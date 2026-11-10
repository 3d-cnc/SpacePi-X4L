#include "anzeige.h"

/* Zeichensatz: Bit 0..6 = Segment a..g */
#define A 0x01
#define B 0x02
#define C 0x04
#define D 0x08
#define E 0x10
#define F 0x20
#define G 0x40

static uint8_t zeichen(char z)
{
    switch (z) {
    case '0': case 'O': return A|B|C|D|E|F;
    case '1': case 'I': return B|C;
    case '2': return A|B|G|E|D;
    case '3': return A|B|G|C|D;
    case '4': return F|G|B|C;
    case '5': case 'S': return A|F|G|C|D;
    case '6': return A|F|G|E|D|C;
    case '7': return A|B|C;
    case '8': case 'B': return A|B|C|D|E|F|G;
    case '9': return A|B|C|D|F|G;
    case '-': return G;
    case 'A': return A|B|C|E|F|G;
    case 'b': return C|D|E|F|G;
    case 'C': return A|D|E|F;
    case 'c': return G|E|D;
    case 'd': return B|C|D|E|G;
    case 'E': return A|D|E|F|G;
    case 'F': return A|E|F|G;
    case 'G': return A|C|D|E|F;
    case 'H': return B|C|E|F|G;
    case 'h': return C|E|F|G;
    case 'J': return B|C|D|E;
    case 'L': return D|E|F;
    case 'n': return C|E|G;
    case 'o': return C|D|E|G;
    case 'P': return A|B|E|F|G;
    case 'r': return E|G;
    case 't': return D|E|F|G;
    case 'U': return B|C|D|E|F;
    case 'u': return C|D|E;
    case 'y': return B|C|D|F|G;
    default:  return 0;
    }
}

/* Normale Ziffer: a 0x80, b 0x40, c 0x20, d 0x10, f 0x08, g 0x04, e 0x02 */
static const uint8_t NORMAL[7] = { 0x80, 0x40, 0x20, 0x10, 0x02, 0x08, 0x04 };   /* a..g */
/* Uhrzeit: linke Seite (f, g, e) im Byte der Ziffer, rechte Seite (a, b, c, d) im naechsten */
static const uint8_t ZEIT[7]   = { 0x08, 0x04, 0x02, 0x01, 0x20, 0x80, 0x40 };   /* a..g */
#define ZEIT_RECHTS (A|B|C|D)

void bild_leeren(uint8_t *b) { for (int i = 0; i < BILD_BYTES; i++) b[i] = 0; }

void bild_symbol(uint8_t *b, uint16_t sym, int an)
{
    uint8_t byte = (uint8_t)(sym >> 8), bit = (uint8_t)sym;
    if (an) b[byte] |= bit; else b[byte] &= (uint8_t)~bit;
}

void bild_ziffer(uint8_t *b, int byte, char z)
{
    uint8_t s = zeichen(z);
    for (int i = 0; i < 7; i++) if (s & (1u << i)) b[byte] |= NORMAL[i];
}

void bild_zeit(uint8_t *b, const char *sechs)
{
    for (int k = 0; k < 6 && sechs[k]; k++) {
        uint8_t s = zeichen(sechs[k]);
        for (int i = 0; i < 7; i++)
            if (s & (1u << i)) b[((1u << i) & ZEIT_RECHTS) ? 3 + k : 2 + k] |= ZEIT[i];
    }
}

void bild_text(uint8_t *b, int erstes_byte, int anzahl, const char *text)
{
    for (int i = 0; i < anzahl && text[i]; i++) bild_ziffer(b, erstes_byte + i, text[i]);
}
