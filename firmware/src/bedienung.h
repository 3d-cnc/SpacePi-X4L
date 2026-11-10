/* Bedienoberflaeche: zwei Kammern, vier Tasten, Segment-Display.
 *
 *   L/R              Ansicht wechseln (linke / rechte Kammer)
 *   Zahnrad kurz     Feld waehlen: Material -> Solltemperatur -> Zeit -> fertig
 *   Zahnrad 1,5 s    Trocknen der angezeigten Kammer starten / stoppen
 *   Pfeile           Wert im gewaehlten Feld aendern (gedrueckt halten = schnell)
 *   8 s ohne Taste   Feld verlassen, Einstellungen speichern
 */
#ifndef BEDIENUNG_H
#define BEDIENUNG_H
#include <stdint.h>

typedef struct {
    int32_t t[2];     /* 0,01 °C, [0] = links, [1] = rechts */
    int32_t rh[2];    /* 0,01 % */
    uint8_t ok[2];    /* 1 = Messwert gueltig */
    uint8_t fehler[2];  /* Heizungsfehler 0..6 */
    uint8_t heizt[2];   /* 1 = Heizung arbeitet gerade (Leistung > 0) */
} messwerte_t;

typedef struct {
    uint8_t  material;
    uint8_t  soll;        /* °C */
    uint16_t dauer_min;
    uint8_t  laeuft;      /* Trocknen aktiv (Stufe 2: ohne Heizung) */
    uint32_t rest_s;
    uint32_t gesamt_s;
} kammer_t;

extern kammer_t kammer[2];
extern uint8_t ansicht;   /* 0 = links, 1 = rechts */
extern uint8_t feld;      /* 0 keins, 1 Material, 2 Temperatur, 3 Zeit */

void bedienung_start(void);
void bedienung_tasten(uint32_t tasten, uint32_t t);      /* alle 20 ms, Bit0 L/R, 1 runter, 2 hoch, 3 Zahnrad */
void bedienung_takt(uint32_t t);                         /* jede Schleife */
void bedienung_bild(uint8_t *bild, const messwerte_t *m, uint32_t t);

/* Fernsteuerung (UART/Home Assistant). Rueckgabe 0 = ok, 1 = ungueltiger Wert,
 * 2 = Kammer hat einen Fehler, 3 = nicht moeglich waehrend die Kammer laeuft.
 * Geaenderte Einstellungen werden 10 s nach der letzten Aenderung gespeichert. */
int fern_start(int k, uint32_t t);
int fern_stop(int k, uint32_t t);
int fern_soll(int k, int grad, uint32_t t);
int fern_dauer(int k, int minuten, uint32_t t);
int fern_material(int k, int index, uint32_t t);
int material_anzahl(void);

#endif
