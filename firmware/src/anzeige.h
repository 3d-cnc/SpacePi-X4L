/* Segment-Display des X4L (YXD-BS0023-3): Bild aus 20 Byte zusammensetzen.
 * Zuordnung am 10.11.2026 per Fotoserie ermittelt und per Testbild bestaetigt
 * (siehe werkzeuge/lcd_bild.py und PINBELEGUNG.md). */
#ifndef ANZEIGE_H
#define ANZEIGE_H
#include <stdint.h>

#define BILD_BYTES 20

/* Byte-Positionen der normalen Ziffern */
enum {
    Z_KLEIN_10 = 0, Z_KLEIN_1 = 1,          /* kleine Temperatur (Sollwert) */
    Z_FEUCHTE_10 = 9, Z_FEUCHTE_1 = 10,
    Z_GROSS_10 = 11, Z_GROSS_1 = 12,        /* grosse Temperatur (Istwert) */
    Z_MATERIAL = 14                         /* 14..19, sechs Zeichen */
};

/* Symbole: oberes Byte = Byte-Nummer, unteres = Bitmaske */
#define SYM(byte, bit) (((byte) << 8) | (bit))
enum {
    S_KLEIN_HUNDERT = SYM(0, 0x01), S_THERMOMETER = SYM(1, 0x01),
    S_L = SYM(2, 0x08), S_TIME = SYM(2, 0x10),
    S_KLEIN_GRAD = SYM(2, 0x02), S_KLEIN_C_UNTEN = SYM(2, 0x01), S_KLEIN_C_MITTE = SYM(2, 0x04),
    S_ZAHNRAD = SYM(3, 0x10), S_DOPPELPUNKT1 = SYM(4, 0x10), S_PFEIL_HOCH = SYM(5, 0x10),
    S_DOPPELPUNKT2 = SYM(6, 0x10), S_PFEIL_RUNTER = SYM(7, 0x10),
    S_RING_OBEN = SYM(8, 0x80), S_RING_OBEN_LINKS = SYM(8, 0x40),
    S_RING_LINKS = SYM(8, 0x20), S_RING_UNTEN_LINKS = SYM(8, 0x10),
    S_TROPFEN = SYM(9, 0x01), S_RH = SYM(10, 0x01), S_GROSS_HUNDERT = SYM(11, 0x01),
    S_PROZENT = SYM(12, 0x01),
    S_RING_OBEN_RECHTS = SYM(13, 0x80), S_R = SYM(13, 0x40), S_RING_RECHTS = SYM(13, 0x20),
    S_RING_UNTEN_RECHTS = SYM(13, 0x10), S_PV = SYM(13, 0x08),
    S_GROSS_C_MITTE = SYM(13, 0x04), S_GROSS_GRAD = SYM(13, 0x02), S_GROSS_C_UNTEN = SYM(13, 0x01),
    S_LR_TASTE = SYM(15, 0x01), S_ON = SYM(16, 0x01), S_AUTO = SYM(17, 0x01),
    S_OFF = SYM(18, 0x01), S_MATERIAL = SYM(19, 0x01)
};

void bild_leeren(uint8_t *b);
void bild_symbol(uint8_t *b, uint16_t sym, int an);
void bild_ziffer(uint8_t *b, int byte, char z);               /* normale Ziffer */
void bild_zeit(uint8_t *b, const char *sechs);                /* T1..T6, ohne Doppelpunkte */
void bild_text(uint8_t *b, int erstes_byte, int anzahl, const char *text);  /* linksbuendig */

#endif
