/* Segment display of the X4L (YXD-BS0023-3): building a frame of 20 bytes.
 * Mapping determined with a photo series and confirmed with a test frame
 * (see tools/lcd_frame.py and docs/HARDWARE.md). */
#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdint.h>

#define FRAME_BYTES 20

/* Byte positions of the normal digits */
enum {
    D_SMALL_10 = 0, D_SMALL_1 = 1,          /* small temperature (setpoint) */
    D_HUMIDITY_10 = 9, D_HUMIDITY_1 = 10,
    D_BIG_10 = 11, D_BIG_1 = 12,            /* big temperature (actual value) */
    D_MATERIAL = 14                         /* 14..19, six characters */
};

/* Symbols: high byte = byte number, low byte = bit mask */
#define SYM(byte, bit) (((byte) << 8) | (bit))
enum {
    S_SMALL_HUNDRED = SYM(0, 0x01), S_THERMOMETER = SYM(1, 0x01),
    S_L = SYM(2, 0x08), S_TIME = SYM(2, 0x10),
    S_SMALL_DEGREE = SYM(2, 0x02), S_SMALL_C_BOTTOM = SYM(2, 0x01), S_SMALL_C_MIDDLE = SYM(2, 0x04),
    S_GEAR = SYM(3, 0x10), S_COLON1 = SYM(4, 0x10), S_ARROW_UP = SYM(5, 0x10),
    S_COLON2 = SYM(6, 0x10), S_ARROW_DOWN = SYM(7, 0x10),
    S_RING_TOP = SYM(8, 0x80), S_RING_TOP_LEFT = SYM(8, 0x40),
    S_RING_LEFT = SYM(8, 0x20), S_RING_BOTTOM_LEFT = SYM(8, 0x10),
    S_DROPLET = SYM(9, 0x01), S_RH = SYM(10, 0x01), S_BIG_HUNDRED = SYM(11, 0x01),
    S_PERCENT = SYM(12, 0x01),
    S_RING_TOP_RIGHT = SYM(13, 0x80), S_R = SYM(13, 0x40), S_RING_RIGHT = SYM(13, 0x20),
    S_RING_BOTTOM_RIGHT = SYM(13, 0x10), S_PV = SYM(13, 0x08),
    S_BIG_C_MIDDLE = SYM(13, 0x04), S_BIG_DEGREE = SYM(13, 0x02), S_BIG_C_BOTTOM = SYM(13, 0x01),
    S_LR_KEY = SYM(15, 0x01), S_ON = SYM(16, 0x01), S_AUTO = SYM(17, 0x01),
    S_OFF = SYM(18, 0x01), S_MATERIAL = SYM(19, 0x01)
};

void frame_clear(uint8_t *b);
void frame_symbol(uint8_t *b, uint16_t sym, int on);
void frame_digit(uint8_t *b, int byte, char c);                    /* normal digit */
void frame_time(uint8_t *b, const char *six);                      /* T1..T6, without colons */
void frame_text(uint8_t *b, int first_byte, int count, const char *text);   /* left-aligned */

#endif
