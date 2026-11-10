/* Heizungsregelung mit Schutzfunktionen, je Kammer.
 *
 * Grundregel des Projekts: NIEMALS heizen ohne Luefter.
 * Der Heizungsausgang wird in main.c nur gesetzt, wenn der Luefterausgang derselben
 * Kammer tatsaechlich High ist und seit mindestens LUEFTER_VORLAUF_MS laeuft.
 *
 * Fehlercodes (Anzeige "Err n" im Materialfeld):
 *   1  Kammersensor (AHT20) liefert keine Werte
 *   2  Heizungsfuehler (NTC) offen oder kurzgeschlossen
 *   3  Heizflaeche zu heiss (>= NTC_FEHLER)
 *   4  Kammer zu heiss (>= KAMMER_FEHLER)
 *   5  Heizung schaltet nicht ab: Heizflaeche wird heisser, obwohl aus  -> Netzstecker ziehen!
 *   6  Heizung heizt nicht: Heizflaeche wird trotz Heizen nicht warm
 * Fehler stoppen das Trocknen der Kammer, Luefter laufen nach. Quittieren mit Zahnrad,
 * Fehler 5 erst nach Neustart. */
#ifndef HEIZUNG_H
#define HEIZUNG_H
#include <stdint.h>
#include "bedienung.h"

#define LUEFTER_VORLAUF_MS  3000u     /* Luefter muss so lange laufen, bevor geheizt wird */
#define NACHLAUF_MIN_MS     60000u    /* Luefter-Nachlauf mindestens */
#define NACHLAUF_MAX_MS     900000u   /* hoechstens 15 min */
#define NACHLAUF_BIS_NTC    450       /* 0,1 °C: Nachlauf bis Heizflaeche unter 45 °C */
#define NTC_REGEL_GRENZE    1200      /* 0,1 °C: ab 120 °C Heizflaeche wird nicht mehr geheizt */
#define NTC_FEHLER          1350      /* 0,1 °C: Original meldet ab 140 °C */
#define KAMMER_FEHLER       8000      /* 0,01 °C: 80 °C (X4L: max. 75 °C) */
#define FENSTER_MS          2000u     /* Zeitproportional: Heizzeit je 2-s-Fenster */

typedef struct {
    uint8_t  fehler;          /* 0 = ok, sonst Code 1..6 */
    uint8_t  luefter_soll;    /* 1 = Luefter an */
    uint8_t  heizen_soll;     /* 1 = Heizung darf/soll jetzt an (vor der Luefter-Sperre in main) */
    uint8_t  leistung;        /* 0..100 % */
    int16_t  ntc_t;           /* 0,1 °C, -32768 = ungueltig */
    int32_t  i_anteil;        /* Integralanteil in 0,01 % */
    uint32_t nachlauf_start;
    uint8_t  nachlauf;
    /* Plausibilitaet */
    uint32_t pruef_start;
    uint8_t  pruef_aktiv;
    uint8_t  aus_aktiv;
    int16_t  pruef_ntc;
    uint32_t aus_seit;        /* gueltig, wenn aus_aktiv */
    int16_t  aus_ntc_min;     /* Tiefstwert der Heizflaeche, erst ab 90 s nach dem Abschalten */
    uint8_t  aus_basis;       /* 1 = Tiefstwert gueltig */
    uint32_t i_zeit;          /* letzte Integration (einmal pro Sekunde) */
    uint8_t  im_pi;           /* 1 = im Regelbereich (I-Anteil ist vorbelegt) */
} heizung_t;

extern heizung_t heizung[2];

int16_t ntc_temperatur(uint32_t adc);   /* 0,1 °C oder -32768 */

/* Einmal pro Hauptschleife. aht_alter_ms = Zeit seit dem letzten gueltigen Messwert. */
void heizung_schritt(int k, uint32_t t, kammer_t *kam, int32_t aht_t, uint32_t aht_alter_ms,
                     uint32_t ntc_adc, uint8_t heizt_wirklich);

void heizung_quittieren(int k);

#endif
