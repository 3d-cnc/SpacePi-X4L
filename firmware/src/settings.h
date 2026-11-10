/* Per-chamber settings stored in the last flash page (0x0801F800). */
#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>

typedef struct {
    uint32_t magic;          /* 0x31453458 = "X4E1" */
    uint8_t  material[2];    /* index into the material list, [0] = left, [1] = right */
    uint8_t  setpoint[2];    /* °C */
    uint16_t duration_min[2];
    uint32_t checksum;
} settings_t;

int  settings_load(settings_t *s);           /* 1 = valid data read */
void settings_save(const settings_t *s);

#endif
