/*
 * qlscreen.c - MorphOS Reggae demuxer class for Sinclair QL screen images
 *
 * Supports both Sinclair QL screen modes:
 *  - Mode 4: 512 x 256, 4 colours (2 bpp)
 *  - Mode 8: 256 x 256, 8 colours (3 bpp)
 *
 * A Sinclair QL screen image is 32768 bytes (256 lines of 128 bytes).
 * There is no fixed header or common extension; files are 32768 bytes.
 */

#define __NOLIBBASE__
#define SYSTEM_PRIVATE

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/multimedia.h>
#include <emul/emulregs.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <clib/alib_protos.h>
#include <clib/debug_protos.h>
#include <classes/multimedia/multimedia.h>
#include <classes/multimedia/video.h>

#include "class_version.h"

#define SUPERCLASS "multimedia.class"

/* Sinclair QL Screen Constants */
#define QL_SCREEN_SIZE         32768
#define QL_SCREEN_LINES        256
#define QL_LINE_BYTES          128

#define QL_MODE_UNKNOWN        0
#define QL_MODE_4              4
#define QL_MODE_8              8

#define QL_MODE4_WIDTH         512
#define QL_MODE4_HEIGHT        256
#define QL_MODE4_BPP           2
#define QL_MODE4_COLORS        4

#define QL_MODE8_WIDTH         256
#define QL_MODE8_HEIGHT        256
#define QL_MODE8_BPP           3
#define QL_MODE8_COLORS        8

/*
 * Sinclair QL 4-colour palette (Mode 4):
 *  0: Black  (#000000)
 *  1: Red    (#FF0000)
 *  2: Green  (#00FF00)
 *  3: White  (#FFFFFF)
 */
static const ULONG QL_Palette_Mode4[QL_MODE4_COLORS] = {
	0xFF000000,
	0xFFFF0000,
	0xFF00FF00,
	0xFFFFFFFF
};

/*
 * Sinclair QL 8-colour palette (Mode 8):
 *  0: Black   (#000000)
 *  1: Blue    (#0000FF)
 *  2: Red     (#FF0000)
 *  3: Magenta (#FF00FF)
 *  4: Green   (#00FF00)
 *  5: Cyan    (#00FFFF)
 *  6: Yellow  (#FFFF00)
 *  7: White   (#FFFFFF)
 */
static const ULONG QL_Palette_Mode8[QL_MODE8_COLORS] = {
	0xFF000000,
	0xFF0000FF,
	0xFFFF0000,
	0xFFFF00FF,
	0xFF00FF00,
	0xFF00FFFF,
	0xFFFFFF00,
	0xFFFFFFFF
};

/* Library bases */
struct Library *SysBase, *IntuitionBase, *UtilityBase, *MultimediaBase;

struct ClassBase
{
	struct Library          LibNode;
	Class                  *LibClass;
	APTR                    Seglist;
	struct SignalSemaphore  BaseLock;
	BOOL                    InitFlag;
	const struct TagItem   *Attributes;
};

struct ObjData
{
	Object      *StreamObj;
	ULONG        Mode;            /* QL_MODE_4 or QL_MODE_8 */
	ULONG        Width;           /* 512 (mode 4) or 256 (mode 8) */
	ULONG        Height;          /* 256 */
	ULONG        BitsPerPixel;    /* 2 (mode 4) or 3 (mode 8) */
	const ULONG *Palette;         /* pointer to palette array */
	ULONG        NumColors;       /* 4 or 8 */
	UQUAD        FrameCount;      /* 1 */
	ULONG        BytesRead;       /* bytes pulled so far (up to 32768) */
	BOOL         HeaderParsed;
};

#define GET_BASE struct ClassBase *cb = (struct ClassBase*)cl->cl_UserData
#define GET_DATA struct ObjData *d = (struct ObjData*)INST_DATA(cl, obj)

static const ULONG InputFormats[] = {
	MMF_STREAM,
	0
};

static const ULONG OutputFormats[] = {
	MMF_VIDEO_QLSCREEN,
	0
};

static inline ULONG xget(Object *obj, ULONG attr)
{
	ULONG val = 0;
	DoMethod(obj, OM_GET, attr, (IPTR)&val);
	return val;
}

#define OBJNAME(obj) xget(obj, MMA_ObjectName)

/* Forward prototypes */
struct Library *LibInit(struct Library *unused, APTR seglist, struct Library *sysb);
struct ClassBase *lib_init(struct ClassBase *cb, APTR seglist, struct Library *SysBase);
APTR lib_expunge(struct ClassBase *cb);
struct Library *LibOpen(void);
ULONG LibClose(void);
APTR LibExpunge(void);
ULONG LibReserved(void);
Class *GetClass(void);
LONG ClassDispatcher(void);
Class *init_class(struct ClassBase *cb);
BOOL InitResources(struct ClassBase *cb);
void FreeResources(struct ClassBase *cb);
LONG New(Class *cl, Object *obj, struct opSet *msg);
LONG Dispose(Class *cl, Object *obj, Msg msg);
LONG Get(Class *cl, Object *obj, struct opGet *msg);
LONG GetPort(Class *cl, Object *obj, struct mmopGetPort *msg);
LONG Pull(Class *cl, Object *obj, struct mmopPull *msg);
LONG Setup(Class *cl, Object *obj, struct mmopSetup *msg);
void QL_DecodeMode4Line(const UBYTE *src, UBYTE *dst);
void QL_DecodeMode8Line(const UBYTE *src, UBYTE *dst);
static ULONG QL_DetectMode(Object *obj, struct ObjData *d);

/* Helper to convert Mode 4 line (128 bytes = 64 word pairs) into 512 8-bit chunky indices (0..3) */
void QL_DecodeMode4Line(const UBYTE *src, UBYTE *dst)
{
	int w;
	for (w = 0; w < 128; w += 2) {
		UBYTE g = src[w];
		UBYTE r = src[w + 1];
		int bit;
		for (bit = 7; bit >= 0; bit--) {
			UBYTE pixel = (UBYTE)((((g >> bit) & 1) << 1) | ((r >> bit) & 1));
			*dst++ = pixel;
		}
	}
}

/* Helper to convert Mode 8 line (128 bytes = 64 word pairs) into 256 8-bit chunky indices (0..7) */
void QL_DecodeMode8Line(const UBYTE *src, UBYTE *dst)
{
	int w;
	for (w = 0; w < 128; w += 2) {
		UBYTE gb = src[w];
		UBYTE rb = src[w + 1];
		int pair;
		for (pair = 6; pair >= 0; pair -= 2) {
			UBYTE g = (gb >> (pair + 1)) & 1;
			UBYTE r = (rb >> (pair + 1)) & 1;
			UBYTE b = (rb >> pair) & 1;
			UBYTE pixel = (UBYTE)((g << 2) | (r << 1) | b);
			*dst++ = pixel;
		}
	}
}

/* Resident and Library structures */
const char LibName[] = CLASSNAME;
char VTag[] = VERSTAG;

static const struct TagItem RTags[] =
{
	{MMA_MediaType,        MMT_PICTURE},
	{MMA_SupportedFormats, (IPTR)InputFormats},
	{TAG_END,              0}
};

struct Resident ROMTag =
{
	RTC_MATCHWORD,
	&ROMTag,
	&ROMTag + 1,
	RTF_EXTENDED | RTF_PPC,
	VERSION,
	NT_LIBRARY,
	0,
	(STRPTR)LibName,
	VSTRING,
	(APTR)LibInit,
	REVISION,
	(const struct TagItem *)RTags
};

APTR JumpTable[] =
{
	(APTR)FUNCARRAY_32BIT_NATIVE,
	(APTR)LibOpen,
	(APTR)LibClose,
	(APTR)LibExpunge,
	(APTR)LibReserved,
	(APTR)GetClass,
	(APTR)0xFFFFFFFF
};

static const struct EmulLibEntry ClassDispatcher_gate =
{
	TRAP_LIB,
	0,
	(void(*)(void))ClassDispatcher
};

Class *init_class(struct ClassBase *cb)
{
	Class *cl = NULL;

	if ((cl = MakeClass(LibName, SUPERCLASS, NULL, sizeof(struct ObjData), 0L)))
	{
		cl->cl_Dispatcher.h_Entry = (HOOKFUNC)&ClassDispatcher_gate;
		cl->cl_UserData = (ULONG)cb;
		AddClass(cl);
	}
	cb->LibClass = cl;

	return cl;
}

BOOL InitResources(struct ClassBase *cb)
{
	if (!(IntuitionBase = OpenLibrary("intuition.library", 50))) return FALSE;
	if (!(UtilityBase = OpenLibrary("utility.library", 50))) return FALSE;
	if (!(MultimediaBase = OpenLibrary("Multimedia/multimedia.class", 50))) return FALSE;
	if (!(init_class(cb))) return FALSE;
	return TRUE;
}

void FreeResources(struct ClassBase *cb)
{
	(void)cb;
	if (MultimediaBase) CloseLibrary(MultimediaBase);
	if (UtilityBase) CloseLibrary(UtilityBase);
	if (IntuitionBase) CloseLibrary(IntuitionBase);
}

struct ClassBase *lib_init(struct ClassBase *cb, APTR seglist, struct Library *sysbase)
{
	InitSemaphore(&cb->BaseLock);
	cb->Seglist = seglist;
	cb->Attributes = 0;
	(void)sysbase;
	return cb;
}

struct Library *LibInit(struct Library *unused, APTR seglist, struct Library *sysbase)
{
	(void)unused;
	SysBase = sysbase;

	return (NewCreateLibraryTags(
		LIBTAG_FUNCTIONINIT, (ULONG)JumpTable,
		LIBTAG_LIBRARYINIT,  (ULONG)lib_init,
		LIBTAG_MACHINE,      MACHINE_PPC,
		LIBTAG_BASESIZE,     sizeof(struct ClassBase),
		LIBTAG_SEGLIST,      (ULONG)seglist,
		LIBTAG_TYPE,         NT_LIBRARY,
		LIBTAG_NAME,         (ULONG)ROMTag.rt_Name,
		LIBTAG_IDSTRING,     (ULONG)ROMTag.rt_IdString,
		LIBTAG_FLAGS,        LIBF_CHANGED | LIBF_SUMUSED,
		LIBTAG_VERSION,      VERSION,
		LIBTAG_REVISION,     REVISION,
		LIBTAG_PUBLIC,       TRUE,
	TAG_END));
}

struct Library *LibOpen(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	struct Library *lib = (struct Library*)cb;

	ObtainSemaphore(&cb->BaseLock);
	if (!cb->InitFlag)
	{
		if (InitResources(cb)) cb->InitFlag = TRUE;
		else
		{
			FreeResources(cb);
			lib = NULL;
		}
	}

	if (lib)
	{
		cb->LibNode.lib_Flags &= ~LIBF_DELEXP;
		cb->LibNode.lib_OpenCnt++;
	}
	ReleaseSemaphore(&cb->BaseLock);
	return lib;
}

ULONG LibClose(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	ULONG ret = 0;

	ObtainSemaphore(&cb->BaseLock);
	if (--cb->LibNode.lib_OpenCnt == 0)
	{
		if (cb->LibNode.lib_Flags & LIBF_DELEXP) ret = (ULONG)lib_expunge(cb);
	}
	ReleaseSemaphore(&cb->BaseLock);

	return ret;
}

APTR LibExpunge(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	return lib_expunge(cb);
}

APTR lib_expunge(struct ClassBase *cb)
{
	APTR seglist = NULL;

	ObtainSemaphore(&cb->BaseLock);
	if (cb->LibNode.lib_OpenCnt == 0)
	{
		if (!cb->LibClass || FreeClass(cb->LibClass))
		{
			cb->LibClass = NULL;
			Forbid();
			Remove((struct Node*)cb);
			Permit();
			FreeResources(cb);
			seglist = cb->Seglist;
			FreeMem((UBYTE*)cb - cb->LibNode.lib_NegSize, cb->LibNode.lib_NegSize + cb->LibNode.lib_PosSize);
			cb = NULL;
		}
		if (cb && cb->LibClass) AddClass(cb->LibClass);
	}
	else
	{
		cb->LibNode.lib_Flags |= LIBF_DELEXP;
	}

	if (cb) ReleaseSemaphore(&cb->BaseLock);
	return seglist;
}

ULONG LibReserved(void)
{
	return 0;
}

Class *GetClass(void)
{
	struct ClassBase *cb = (struct ClassBase*)REG_A6;
	return cb->LibClass;
}

/*
 * Detect Sinclair QL screen mode:
 * 1. Checks filename hint (e.g. "mode4" or "mode8" in name).
 * 2. Peeks initial scanlines from stream: In Mode 8, flash bits (bits 6, 4, 2, 0 of even bytes)
 *    are 0 for non-flashing graphics. In Mode 4, the even byte is a continuous green bitplane,
 *    so flash bits are set on average ~50% of the time for content (> 5%).
 */
static ULONG QL_DetectMode(Object *obj, struct ObjData *d)
{
	UQUAD streamLength = 0;
	STRPTR streamName = NULL;
	UBYTE peek_buf[512];
	LONG peeked;

	streamLength = MediaGetPort64(obj, 0, MMA_StreamLength);
	if (streamLength != 0 && streamLength != QL_SCREEN_SIZE) {
		return QL_MODE_UNKNOWN;
	}

	/* Check filename hints */
	DoMethod(obj, MMM_GetPort, 0, MMA_StreamName, (IPTR)&streamName);
	if (streamName) {
		ULONG len = 0;
		while (streamName[len]) len++;
		if (len >= 5) {
			if (strcasecmp(&streamName[len - 5], "mode8") == 0) return QL_MODE_8;
			if (strcasecmp(&streamName[len - 5], "mode4") == 0) return QL_MODE_4;
		}
		if (len >= 6) {
			if (strcasecmp(&streamName[len - 6], ".mode8") == 0) return QL_MODE_8;
			if (strcasecmp(&streamName[len - 6], ".mode4") == 0) return QL_MODE_4;
		}
	}

	/* Peek initial bytes from input stream to check flash bits */
	peeked = DoMethod(d->StreamObj, MMM_Peek, 0, (IPTR)peek_buf, sizeof(peek_buf));
	if (peeked >= 128) {
		ULONG flash_count = 0;
		ULONG total_tested = 0;
		LONG i;
		for (i = 0; i < peeked; i += 2) {
			UBYTE gb = peek_buf[i];
			flash_count += ((gb >> 6) & 1) + ((gb >> 4) & 1) + ((gb >> 2) & 1) + (gb & 1);
			total_tested += 4;
		}
		DoMethod(d->StreamObj, MMM_Restore);

		/* If flash bit ratio >= 5%, this is Mode 4 (bitplane data); otherwise Mode 8 */
		if (total_tested > 0 && (flash_count * 100 / total_tested) >= 5) {
			return QL_MODE_4;
		} else {
			return QL_MODE_8;
		}
	}

	/* Default fallback */
	return QL_MODE_4;
}

/* BOOPSI methods */

LONG New(Class *cl, Object *obj, struct opSet *msg)
{
	LONG newobj = 0;

	if ((obj = (Object*)DoSuperMethodA(cl, obj, (Msg)msg)))
	{
		GET_DATA;
		DoMethod(obj, MMM_LockObject);

		d->StreamObj = (Object*)GetTagData(MMA_Stream, 0, msg->ops_AttrList);
		d->Mode = QL_MODE_4;
		d->Width = QL_MODE4_WIDTH;
		d->Height = QL_MODE4_HEIGHT;
		d->BitsPerPixel = QL_MODE4_BPP;
		d->Palette = QL_Palette_Mode4;
		d->NumColors = QL_MODE4_COLORS;
		d->FrameCount = 1;
		d->BytesRead = 0;
		d->HeaderParsed = FALSE;

		/* Input port: Port 0 (MMF_STREAM) */
		DoMethod(obj, MMM_AddPort, 0);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_Type, MDP_TYPE_INPUT);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_FormatsTable, (IPTR)InputFormats);
		DoMethod(obj, MMM_SetPort, 0, MMA_Port_Format, MMF_STREAM);

		/* Output port: Port 1 (MMF_VIDEO_QLSCREEN) */
		DoMethod(obj, MMM_AddPort, 1);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_Type, MDP_TYPE_OUTPUT);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_FormatsTable, (IPTR)OutputFormats);
		DoMethod(obj, MMM_SetPort, 1, MMA_Port_Format, MMF_VIDEO_QLSCREEN);

		newobj = (LONG)obj;
		DoMethod(obj, MMM_UnlockObject);
	}

	if (!newobj) CoerceMethod(cl, obj, OM_DISPOSE);
	else MLOGV(LOG_INFO, "Object "%s" created.", OBJNAME((Object*)newobj));

	return newobj;
}

LONG Dispose(Class *cl, Object *obj, Msg msg)
{
	DoMethod(obj, MMM_LockObject);
	MLOGV(LOG_INFO, "Object "%s" disposed.", OBJNAME(obj));
	DoMethod(obj, MMM_UnlockObject);
	return DoSuperMethodA(cl, obj, msg);
}

LONG Setup(Class *cl, Object *obj, struct mmopSetup *msg)
{
	GET_DATA;
	LONG rv = 0;

	MLOGV(LOG_VERBOSE, "Started for port %lu.", msg->Port);

	if (msg->Port == 0)
	{
		rv = DoSuperMethodA(cl, obj, (Msg)msg);
	}
	else if (msg->Port == 1)
	{
		if (!d->HeaderParsed)
		{
			ULONG mode = QL_DetectMode(obj, d);
			if (mode == QL_MODE_8)
			{
				d->Mode = QL_MODE_8;
				d->Width = QL_MODE8_WIDTH;
				d->BitsPerPixel = QL_MODE8_BPP;
				d->Palette = QL_Palette_Mode8;
				d->NumColors = QL_MODE8_COLORS;
			}
			else
			{
				d->Mode = QL_MODE_4;
				d->Width = QL_MODE4_WIDTH;
				d->BitsPerPixel = QL_MODE4_BPP;
				d->Palette = QL_Palette_Mode4;
				d->NumColors = QL_MODE4_COLORS;
			}
			d->HeaderParsed = TRUE;
		}
		rv = TRUE;
	}
	else
	{
		MLOGV(LOG_ERRORS, "Setup on non existing port %ld.", msg->Port);
	}

	return rv;
}

LONG Get(Class *cl, Object *obj, struct opGet *msg)
{
	GET_DATA;

	switch (msg->opg_AttrID)
	{
		case MMA_Video_FrameCount:
			*(UQUAD*)msg->opg_Storage = d->FrameCount;
			return TRUE;

		case MMA_Video_Height:
			*msg->opg_Storage = d->Height;
			return TRUE;

		case MMA_Video_Width:
			*msg->opg_Storage = d->Width;
			return TRUE;

		case MMA_Video_BitsPerPixel:
			*msg->opg_Storage = d->BitsPerPixel;
			return TRUE;

		case MMA_Video_Palette:
			*(const ULONG**)msg->opg_Storage = d->Palette;
			return TRUE;

		case MMA_MediaType:
			*msg->opg_Storage = MMT_PICTURE;
			return TRUE;

		default:
			return DoSuperMethodA(cl, obj, (Msg)msg);
	}
}

LONG GetPort(Class *cl, Object *obj, struct mmopGetPort *msg)
{
	switch (msg->Attribute)
	{
		case MMA_Video_Height:
		case MMA_Video_Width:
		case MMA_Video_BitsPerPixel:
		case MMA_Video_FrameCount:
		case MMA_Video_Palette:
		case MMA_MediaType:
			return DoMethod(obj, OM_GET, msg->Attribute, (IPTR)msg->Storage);
	}
	return DoSuperMethodA(cl, obj, (Msg)msg);
}

LONG Pull(Class *cl, Object *obj, struct mmopPull *msg)
{
	GET_DATA;
	ULONG bytes_pulled = 0;

	MLOGV(LOG_VERBOSE, "-> Pull on port %lu of '%s', %lu bytes to $%08lX.",
		msg->Port, OBJNAME(obj), msg->Length, msg->Buffer);

	DoMethod(obj, MMM_LockObject);
	SETERR(0);

	if (msg->Buffer && msg->Length)
	{
		switch (msg->Port)
		{
			case 0:
				bytes_pulled = DoSuperMethodA(cl, obj, (Msg)msg);
				break;

			case 1:
				if (d->BytesRead < QL_SCREEN_SIZE)
				{
					LONG to_read = msg->Length;
					if ((ULONG)(d->BytesRead + to_read) > QL_SCREEN_SIZE)
					{
						to_read = QL_SCREEN_SIZE - d->BytesRead;
					}
					/* Forward pull to connected stream object at port 0 */
					bytes_pulled = DoMethod(d->StreamObj, MMM_Pull, 0, msg->Buffer, to_read);
					d->BytesRead += bytes_pulled;

					if ((LONG)bytes_pulled < to_read)
					{
						SETERR(MediaGetPortFwd(obj, 0, MMA_ErrorCode));
					}
				}
				break;

			default:
				SETERR(MMERR_WRONG_ARGUMENTS);
				MLOGV(LOG_ERRORS, "'%s' has no port %lu.", OBJNAME(obj), msg->Port);
				break;
		}
	}
	else
	{
		MLOG(LOG_ERRORS, "NULL buffer or zero length.");
		SETERR(MMERR_WRONG_ARGUMENTS);
	}

	DoMethod(obj, MMM_UnlockObject);

	MLOGV(LOG_VERBOSE, "<- Pull on port %lu of '%s' finished, %lu bytes delivered.",
		msg->Port, OBJNAME(obj), bytes_pulled);

	return bytes_pulled;
}

LONG ClassDispatcher(void)
{
	Class *cl = (Class*)REG_A0;
	Object *obj = (Object*)REG_A2;
	Msg msg = (Msg)REG_A1;

	switch (msg->MethodID)
	{
		case OM_NEW:      return New(cl, obj, (struct opSet*)msg);
		case OM_DISPOSE:  return Dispose(cl, obj, msg);
		case OM_GET:      return Get(cl, obj, (struct opGet*)msg);
		case MMM_Pull:    return Pull(cl, obj, (struct mmopPull*)msg);
		case MMM_Setup:   return Setup(cl, obj, (struct mmopSetup*)msg);
		case MMM_GetPort: return GetPort(cl, obj, (struct mmopGetPort*)msg);
		default:          return DoSuperMethodA(cl, obj, msg);
	}
}
