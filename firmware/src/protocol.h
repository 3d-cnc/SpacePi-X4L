/* Line protocol over the UART header J26 (PB6 TX, PB7 RX, 115200 8N1) to the ESP32-C3.
 *
 * Dryer -> ESP, every second per chamber, and the version every 10 s:
 *   $K,<k>,<running>,<setpoint>,<duration_min>,<remaining_s>,<material>,<t*100>,<rh*100>,
 *      <heater_surface*10>,<power>,<fault>,<fan>,<sensor_ok>*HH
 *   $V,<major>.<minor>.<patch>*HH
 * ESP -> dryer (checksum mandatory, otherwise the line is discarded):
 *   !START,<k>*HH  !STOP,<k>*HH  !SOLL,<k>,<°C>*HH  !DAUER,<k>,<min>*HH  !MAT,<k>,<n>*HH  !QUIT,<k>*HH
 *   reply: $OK,<COMMAND>,<k>*HH  or  $ERR,<COMMAND>,<reason>*HH
 * k = 0 left, 1 right. HH = XOR of all characters between '$'/'!' and '*', two hex digits.
 *
 * The command and reply tokens are German abbreviations and stay unchanged so that all
 * firmware versions understand each other: SOLL = setpoint, DAUER = duration, MAT = material,
 * QUIT = acknowledge fault; reasons KAMMER = invalid chamber, WERT = invalid value,
 * FEHLER = chamber has a fault, LAEUFT = not possible while running, UNBEKANNT = unknown command.
 *
 * Safety stays in the dryer: even when started remotely it only heats with the fan running
 * and within all limits (heater.c). */
#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>

typedef struct {
    uint8_t  running, setpoint, material, fault, power, fan, sensor_ok;
    uint16_t duration_min;
    uint32_t remaining_s;
    int32_t  t, rh;      /* 0.01 */
    int32_t  ntc;        /* 0.1 °C, -32768 = invalid */
} chamber_status_t;

void protocol_send_status(const chamber_status_t s[2]);
void protocol_send_version(uint32_t version);
void protocol_commands(uint32_t t);     /* process received lines */
uint32_t protocol_commands_total(void);
uint32_t protocol_errors_total(void);

#endif
