/*
 * test_qlscreen.c - Unit tests for Sinclair QL screen decoding and demuxer routines
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef uint8_t  UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;

#define QL_SCREEN_SIZE   32768
#define QL_SCREEN_LINES  256
#define QL_LINE_BYTES    128

#define QL_MODE4_WIDTH   512
#define QL_MODE4_COLORS  4

#define QL_MODE8_WIDTH   256
#define QL_MODE8_COLORS  8

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

/* Functions under test (from qlscreen.c) */
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

void test_decode_mode4_line(void)
{
    UBYTE src[128];
    UBYTE dst[512];
    int i;

    memset(src, 0, sizeof(src));
    memset(dst, 0xFF, sizeof(dst));

    /* Case 1: All zeroes -> all pixels black (color 0) */
    QL_DecodeMode4Line(src, dst);
    for (i = 0; i < 512; i++) {
        assert(dst[i] == 0);
    }

    /* Case 2: Green byte = 0xFF, Red byte = 0x00 -> all pixels green (color 2) */
    src[0] = 0xFF; /* G */
    src[1] = 0x00; /* R */
    QL_DecodeMode4Line(src, dst);
    for (i = 0; i < 8; i++) {
        assert(dst[i] == 2);
    }
    for (i = 8; i < 512; i++) {
        assert(dst[i] == 0);
    }

    /* Case 3: Green byte = 0x00, Red byte = 0xFF -> all pixels red (color 1) */
    src[0] = 0x00;
    src[1] = 0xFF;
    QL_DecodeMode4Line(src, dst);
    for (i = 0; i < 8; i++) {
        assert(dst[i] == 1);
    }

    /* Case 4: Green byte = 0xFF, Red byte = 0xFF -> all pixels white (color 3) */
    src[0] = 0xFF;
    src[1] = 0xFF;
    QL_DecodeMode4Line(src, dst);
    for (i = 0; i < 8; i++) {
        assert(dst[i] == 3);
    }

    /* Case 5: Alternating bits
     * Green = 10101010 (0xAA)
     * Red   = 01010101 (0x55)
     * Pixels: 2, 1, 2, 1, 2, 1, 2, 1
     */
    src[0] = 0xAA;
    src[1] = 0x55;
    QL_DecodeMode4Line(src, dst);
    for (i = 0; i < 8; i++) {
        if (i % 2 == 0) {
            assert(dst[i] == 2);
        } else {
            assert(dst[i] == 1);
        }
    }

    printf("PASS: Mode 4 line decoding tests\n");
}

void test_decode_mode8_line(void)
{
    UBYTE src[128];
    UBYTE dst[256];
    int i;

    memset(src, 0, sizeof(src));
    memset(dst, 0xFF, sizeof(dst));

    /* Case 1: All zeroes -> all pixels black (color 0) */
    QL_DecodeMode8Line(src, dst);
    for (i = 0; i < 256; i++) {
        assert(dst[i] == 0);
    }

    /* Test all 8 colors across 2 word pairs (8 pixels) */
    /* Pixel 0: Black   (000) -> gb bit 7=0, rb bit 7=0, bit 6=0 */
    /* Pixel 1: Blue    (001) -> gb bit 5=0, rb bit 5=0, bit 4=1 */
    /* Pixel 2: Red     (010) -> gb bit 3=0, rb bit 3=1, bit 2=0 */
    /* Pixel 3: Magenta (011) -> gb bit 1=0, rb bit 1=1, bit 0=1 */
    src[0] = 0x00; /* G and flash bits for 4 pixels */
    src[1] = (0 << 6) | (1 << 4) | (2 << 2) | 3; /* 0b00011011 = 0x1B */

    /* Pixel 4: Green   (100) -> gb bit 7=1, rb bit 7=0, bit 6=0 */
    /* Pixel 5: Cyan    (101) -> gb bit 5=1, rb bit 5=0, bit 4=1 */
    /* Pixel 6: Yellow  (110) -> gb bit 3=1, rb bit 3=1, bit 2=0 */
    /* Pixel 7: White   (111) -> gb bit 1=1, rb bit 1=1, bit 0=1 */
    src[2] = 0xAA; /* bits 7, 5, 3, 1 set (all G=1, Flash=0) */
    src[3] = (0 << 6) | (1 << 4) | (2 << 2) | 3; /* 0x1B */

    QL_DecodeMode8Line(src, dst);

    for (i = 0; i < 8; i++) {
        assert(dst[i] == i);
    }
    for (i = 8; i < 256; i++) {
        assert(dst[i] == 0);
    }

    printf("PASS: Mode 8 line decoding tests\n");
}

void test_full_screen_size_and_palettes(void)
{
    /* Check screen dimensions and constraints */
    assert(QL_SCREEN_SIZE == 32768);
    assert(QL_SCREEN_LINES == 256);
    assert(QL_LINE_BYTES == 128);
    assert(QL_SCREEN_LINES * QL_LINE_BYTES == QL_SCREEN_SIZE);

    assert(QL_MODE4_WIDTH == 512);
    assert(QL_MODE4_COLORS == 4);
    assert(QL_MODE8_WIDTH == 256);
    assert(QL_MODE8_COLORS == 8);

    /* Verify palette mappings */
    assert(QL_Palette_Mode4[0] == 0xFF000000); /* Black */
    assert(QL_Palette_Mode4[1] == 0xFFFF0000); /* Red */
    assert(QL_Palette_Mode4[2] == 0xFF00FF00); /* Green */
    assert(QL_Palette_Mode4[3] == 0xFFFFFFFF); /* White */

    assert(QL_Palette_Mode8[0] == 0xFF000000); /* Black */
    assert(QL_Palette_Mode8[1] == 0xFF0000FF); /* Blue */
    assert(QL_Palette_Mode8[2] == 0xFFFF0000); /* Red */
    assert(QL_Palette_Mode8[3] == 0xFFFF00FF); /* Magenta */
    assert(QL_Palette_Mode8[4] == 0xFF00FF00); /* Green */
    assert(QL_Palette_Mode8[5] == 0xFF00FFFF); /* Cyan */
    assert(QL_Palette_Mode8[6] == 0xFFFFFF00); /* Yellow */
    assert(QL_Palette_Mode8[7] == 0xFFFFFFFF); /* White */

    printf("PASS: Screen dimensions and constants tests\n");
}

int main(void)
{
    printf("Running Sinclair QL Demuxer Unit Tests...\n");
    test_decode_mode4_line();
    test_decode_mode8_line();
    test_full_screen_size_and_palettes();
    printf("All tests passed successfully!\n");
    return 0;
}
