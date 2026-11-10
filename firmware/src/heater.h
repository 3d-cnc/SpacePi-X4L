/* Heater control with protective functions, per chamber.
 *
 * Ground rule of this project: NEVER heat without the fan.
 * main.c only sets a heater output when the fan output of the same chamber is actually
 * high and has been running for at least FAN_LEAD_MS.
 *
 * Fault codes (shown as "Err n" in the material field):
 *   1  chamber sensor (AHT20) delivers no readings
 *   2  heater sensor (NTC) open or shorted
 *   3  heater surface too hot (>= NTC_FAULT)
 *   4  chamber too hot (>= CHAMBER_FAULT)
 *   5  heater does not switch off: surface keeps heating although off  -> unplug the dryer!
 *   6  heater does not heat: surface does not warm up although heating
 * Faults stop drying in that chamber, the fan keeps running. Acknowledge with the gear key,
 * fault 5 only clears after a restart. */
#ifndef HEATER_H
#define HEATER_H
#include <stdint.h>
#include "ui.h"

#define FAN_LEAD_MS          3000u     /* the fan must run this long before heating */
#define AFTERRUN_MIN_MS      60000u    /* minimum fan after-run */
#define AFTERRUN_MAX_MS      900000u   /* at most 15 min */
#define AFTERRUN_UNTIL_NTC   450       /* 0.1 °C: after-run until the heater surface is below 45 °C */
#define NTC_CONTROL_LIMIT    1200      /* 0.1 °C: no heating from 120 °C heater surface */
#define NTC_FAULT            1350      /* 0.1 °C: fault 3 */
#define CHAMBER_FAULT        8000      /* 0.01 °C: 80 °C (X4L: max. 75 °C) */
#define WINDOW_MS            2000u     /* time-proportional control: on-time per 2 s window */

typedef struct {
    uint8_t  fault;           /* 0 = ok, otherwise code 1..6 */
    uint8_t  fan_request;     /* 1 = fan on */
    uint8_t  heat_request;    /* 1 = heater may/should be on now (before the fan interlock in main) */
    uint8_t  power;           /* 0..100 % */
    int16_t  ntc_t;           /* 0.1 °C, -32768 = invalid */
    int32_t  i_term;          /* integral term in 0.01 % */
    uint32_t afterrun_start;
    uint8_t  afterrun;
    /* plausibility checks */
    uint32_t check_start;
    uint8_t  check_active;
    uint8_t  off_active;
    int16_t  check_ntc;
    uint32_t off_since;       /* valid when off_active */
    int16_t  off_ntc_min;     /* lowest heater surface temperature, only from 90 s after switch-off */
    uint8_t  off_base;        /* 1 = minimum is valid */
    uint32_t i_time;          /* last integration (once per second) */
    uint8_t  in_pi;           /* 1 = in the control band (integral term preloaded) */
} heater_t;

extern heater_t heater[2];

int16_t ntc_temperature(uint32_t adc);   /* 0.1 °C or -32768 */

/* Once per main loop. aht_age_ms = time since the last valid chamber reading. */
void heater_step(int k, uint32_t t, chamber_t *ch, int32_t aht_t, uint32_t aht_age_ms,
                 uint32_t ntc_adc, uint8_t actually_heating);

void heater_acknowledge(int k);

#endif
