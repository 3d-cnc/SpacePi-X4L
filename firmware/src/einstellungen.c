#include "einstellungen.h"
#include "regs.h"

#define EINST_ADR  0x0801F800u
#define EINST_MAGIC 0x31453458u

static uint32_t summe(const einstellungen_t *e)
{
    const uint8_t *p = (const uint8_t *)e;
    uint32_t s = 0x5A5A5A5Au;
    for (unsigned i = 0; i < sizeof(*e) - 4u; i++) s = (s << 5) + (s >> 27) + p[i];
    return s;
}

int einst_laden(einstellungen_t *e)
{
    const einstellungen_t *f = (const einstellungen_t *)EINST_ADR;
    if (f->magic != EINST_MAGIC || f->pruefsumme != summe(f)) return 0;
    *e = *f;
    return 1;
}

static void warten(void) { while (FLASH_SR & 1u) IWDG_KR = 0xAAAA; }

void einst_speichern(const einstellungen_t *e)
{
    einstellungen_t neu = *e;
    neu.magic = EINST_MAGIC;
    neu.pruefsumme = summe(&neu);
    const einstellungen_t *f = (const einstellungen_t *)EINST_ADR;
    const uint8_t *a = (const uint8_t *)f, *b = (const uint8_t *)&neu;
    unsigned gleich = 1;
    for (unsigned i = 0; i < sizeof(neu); i++) if (a[i] != b[i]) { gleich = 0; break; }
    if (gleich) return;                                  /* Flash schonen */

    FLASH_KEYR = 0x45670123u; FLASH_KEYR = 0xCDEF89ABu;   /* entsperren */
    warten();
    FLASH_SR = 0x34u;                                     /* alte Fehlerflags loeschen */
    FLASH_CR |= 1u << 1;                                  /* Seite loeschen */
    FLASH_AR = EINST_ADR;
    FLASH_CR |= 1u << 6;
    warten();
    FLASH_CR &= ~(1u << 1);
    FLASH_CR |= 1u << 0;                                  /* programmieren, 16 Bit */
    const uint16_t *q = (const uint16_t *)&neu;
    volatile uint16_t *z = (volatile uint16_t *)EINST_ADR;
    for (unsigned i = 0; i < sizeof(neu) / 2u; i++) { z[i] = q[i]; warten(); }
    FLASH_CR &= ~1u;
    FLASH_CR |= 1u << 7;                                  /* wieder sperren */
}
