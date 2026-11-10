/* Zeilenprotokoll ueber die UART-Leiste (PB6 TX, PB7 RX, 115200 8N1) zum ESP32-C3.
 *
 * Trockner -> ESP, jede Sekunde je Kammer und alle 10 s die Version:
 *   $K,<k>,<laeuft>,<soll>,<dauer_min>,<rest_s>,<material>,<t*100>,<rh*100>,<heizflaeche*10>,
 *      <leistung>,<fehler>,<luefter>,<sensor_ok>*HH
 *   $V,<major>.<minor>.<patch>*HH
 * ESP -> Trockner (Pruefsumme Pflicht, sonst wird die Zeile verworfen):
 *   !START,<k>*HH  !STOP,<k>*HH  !SOLL,<k>,<C>*HH  !DAUER,<k>,<min>*HH  !MAT,<k>,<n>*HH  !QUIT,<k>*HH
 *   Antwort: $OK,<BEFEHL>,<k>*HH  oder  $ERR,<BEFEHL>,<grund>*HH
 * k = 0 links, 1 rechts. HH = XOR aller Zeichen zwischen '$'/'!' und '*', zweistellig hex.
 *
 * Die Sicherheit bleibt im Trockner: auch ferngestartet heizt er nur mit Luefter und
 * innerhalb aller Grenzen (heizung.c). */
#ifndef PROTOKOLL_H
#define PROTOKOLL_H
#include <stdint.h>

typedef struct {
    uint8_t  laeuft, soll, material, fehler, leistung, luefter, sensor_ok;
    uint16_t dauer_min;
    uint32_t rest_s;
    int32_t  t, rh;      /* 0,01 */
    int32_t  ntc;        /* 0,1 °C, -32768 = ungueltig */
} kammer_status_t;

void protokoll_senden_status(const kammer_status_t s[2]);
void protokoll_senden_version(uint32_t version);
void protokoll_befehle(uint32_t t);     /* empfangene Zeilen abarbeiten */
uint32_t protokoll_befehle_gesamt(void);
uint32_t protokoll_fehler_gesamt(void);

#endif
