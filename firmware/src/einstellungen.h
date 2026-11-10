/* Einstellungen je Kammer in der letzten Flash-Seite (0x0801F800). */
#ifndef EINSTELLUNGEN_H
#define EINSTELLUNGEN_H
#include <stdint.h>

typedef struct {
    uint32_t magic;          /* 0x31453458 = "X4E1" */
    uint8_t  material[2];    /* Index in die Materialliste, [0] = links, [1] = rechts */
    uint8_t  soll[2];        /* °C */
    uint16_t dauer_min[2];
    uint32_t pruefsumme;
} einstellungen_t;

int  einst_laden(einstellungen_t *e);           /* 1 = gueltig gelesen */
void einst_speichern(const einstellungen_t *e);

#endif
