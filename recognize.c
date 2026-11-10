/*
 * recognize.c - Recognition code for Sinclair QL screen files (.dtcode section)
 *
 * Sinclair QL screen files have no standard header or common file extension,
 * but are always exactly 32768 bytes (256 lines x 128 bytes, 0x8000 bytes).
 * Recognition checks for this exact stream length.
 */

#define SYSTEM_PRIVATE

#include <proto/intuition.h>
#include <proto/multimedia.h>
#include <clib/alib_protos.h>

#include "class_version.h"

#define QL_SCREEN_SIZE 32768

ULONG Recognize(struct DtCodeContext *dcc, ULONG recog_type);

static const char FormatDescription[] = "Sinclair QL Screen";
static const char MimeTypeString[]    = "image/x-sinclairql-screen";

const struct TagItem ClassTags[] = {
	{MMA_RecognizeCode, (IPTR)Recognize},
	{MMA_MediaType,     MMT_PICTURE},
	{MMA_ClassType,     MMCLASS_DEMUXER},
	{MMA_DataFormat,    (IPTR)FormatDescription},
	{MMA_MimeType,      (IPTR)MimeTypeString},
	{TAG_END,           0}
};

const struct TagItem* ClassAttributes(void)
{
	return ClassTags;
}

ULONG Recognize(struct DtCodeContext *dcc, ULONG recog_type)
{
	struct Library *IntuitionBase = dcc->dcc_IntuitionBase;
	struct Library *MultimediaBase = dcc->dcc_MultimediaBase;
	UQUAD stream_length;

	(void)recog_type;
	(void)IntuitionBase;
	(void)MultimediaBase;

	stream_length = MediaGetPort64(dcc->dcc_Source, dcc->dcc_Port, MMA_StreamLength);

	/* Sinclair QL screen dump has no header or common extension; recognize by exact length */
	if (stream_length == QL_SCREEN_SIZE)
	{
		return 10000;
	}

	return 0;
}
