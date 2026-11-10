# $VER: makefile 51.1 (10.11.2026)

VERSION = 51

CC = ppc-morphos-gcc
CFLAGS = -noixemul -nostartfiles -O2 -fomit-frame-pointer -mno-prototype -mcpu=604e -mmultiple
CFLAGS += -W -Wall -Wpointer-arith -Wno-parentheses
LD = ppc-morphos-gcc
LDFLAGS = -nostartfiles -noixemul
LIBS = -labox -lm
STRIP = ppc-morphos-strip --remove-section .comment
OUTPUT = qlscreen.demuxer
OBJS = qlscreen.o

.PHONY: all clean install

all: $(OUTPUT)

clean:
	-rm -rf $(OBJS) *.bak *.s *.db $(OUTPUT) recognize recognize.db

install: all
	mkdir -p /SYS/MorphOS/Classes/Multimedia
	cp $(OUTPUT) /SYS/MorphOS/Classes/Multimedia/
	-flushlib $(OUTPUT)

$(OUTPUT).db: $(OBJS) recognize
	$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $(OUTPUT).db
	ppc-morphos-objcopy --add-section .dtcode=recognize $(OUTPUT).db
	ppc-morphos-objcopy --set-section-flags .dtcode=readonly $(OUTPUT).db

$(OUTPUT): $(OUTPUT).db
	$(STRIP) $(OUTPUT).db -o $(OUTPUT)

recognize.db: recognize.c class_version.h
	$(CC) -s -O2 -nostdlib -o recognize.db recognize.c -labox

recognize: recognize.db
	ppc-morphos-objcopy -R .comment recognize.db recognize

qlscreen.o: qlscreen.c class_version.h
	$(CC) $(CFLAGS) -c $<
