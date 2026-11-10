#include "settings.h"
#include "regs.h"

#define SETTINGS_ADDR  0x0801F800u
#define SETTINGS_MAGIC 0x31453458u   /* unchanged since 1.1.0 so stored settings stay valid */

static uint32_t checksum(const settings_t *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t sum = 0x5A5A5A5Au;
    for (unsigned i = 0; i < sizeof(*s) - 4u; i++) sum = (sum << 5) + (sum >> 27) + p[i];
    return sum;
}

int settings_load(settings_t *s)
{
    const settings_t *f = (const settings_t *)SETTINGS_ADDR;
    if (f->magic != SETTINGS_MAGIC || f->checksum != checksum(f)) return 0;
    *s = *f;
    return 1;
}

static void wait_ready(void) { while (FLASH_SR & 1u) IWDG_KR = 0xAAAA; }

void settings_save(const settings_t *s)
{
    settings_t fresh = *s;
    fresh.magic = SETTINGS_MAGIC;
    fresh.checksum = checksum(&fresh);
    const settings_t *f = (const settings_t *)SETTINGS_ADDR;
    const uint8_t *a = (const uint8_t *)f, *b = (const uint8_t *)&fresh;
    unsigned same = 1;
    for (unsigned i = 0; i < sizeof(fresh); i++) if (a[i] != b[i]) { same = 0; break; }
    if (same) return;                                     /* spare the flash */

    FLASH_KEYR = 0x45670123u; FLASH_KEYR = 0xCDEF89ABu;   /* unlock */
    wait_ready();
    FLASH_SR = 0x34u;                                     /* clear old error flags */
    FLASH_CR |= 1u << 1;                                  /* page erase */
    FLASH_AR = SETTINGS_ADDR;
    FLASH_CR |= 1u << 6;
    wait_ready();
    FLASH_CR &= ~(1u << 1);
    FLASH_CR |= 1u << 0;                                  /* program, 16 bit */
    const uint16_t *src = (const uint16_t *)&fresh;
    volatile uint16_t *dst = (volatile uint16_t *)SETTINGS_ADDR;
    for (unsigned i = 0; i < sizeof(fresh) / 2u; i++) { dst[i] = src[i]; wait_ready(); }
    FLASH_CR &= ~1u;
    FLASH_CR |= 1u << 7;                                  /* lock again */
}
