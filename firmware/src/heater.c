#include "heater.h"
#include "ntc_table.h"

#define INVALID (-32768)

heater_t heater[2];

int16_t ntc_temperature(uint32_t adc)
{
    if (adc < 100u || adc > 4050u) return INVALID;        /* shorted / open */
    uint32_t r = 4700u * adc / (4096u - adc);
    unsigned i = 0;
    while (i < NTC_POINTS && r < NTC_OHM[i]) i++;
    if (i == 0 || i == NTC_POINTS) return INVALID;        /* outside the curve */
    uint32_t upper = NTC_OHM[i - 1], lower = NTC_OHM[i];
    return (int16_t)((int32_t)((upper - r) * 10u / (upper - lower)) + (int32_t)i * 10 - 160);
}

static void set_fault(heater_t *h, chamber_t *ch, uint8_t code, uint32_t t)
{
    if (h->fault != 5) h->fault = code;   /* the most serious fault stays */
    if (ch->running) { ch->running = 0; ch->remaining_s = 0; }
    h->afterrun = 1; h->afterrun_start = t;
}

void heater_acknowledge(int k)
{
    if (heater[k].fault && heater[k].fault != 5) heater[k].fault = 0;
}

void heater_step(int k, uint32_t t, chamber_t *ch, int32_t aht_t, uint32_t aht_age_ms,
                 uint32_t ntc_adc, uint8_t actually_heating)
{
    heater_t *h = &heater[k];
    h->ntc_t = ntc_temperature(ntc_adc);

    /* ---- protection, independent of the operating state ---- */
    if (h->ntc_t == INVALID) {
        if (ch->running || actually_heating) set_fault(h, ch, 2, t);
    } else {
        if (h->ntc_t >= NTC_FAULT) set_fault(h, ch, 3, t);
        /* Fault 5: heater is off, the residual heat (up to 90 s) is over, and the heater surface
         * still rises above 60 °C and more than 10 K above its minimum after that. */
        if (actually_heating || !h->off_active) { h->off_since = t; h->off_active = 1; h->off_base = 0; }
        else if (t - h->off_since >= 90000u) {
            if (!h->off_base || h->ntc_t < h->off_ntc_min) { h->off_ntc_min = h->ntc_t; h->off_base = 1; }
            if (h->ntc_t > 600 && h->ntc_t - h->off_ntc_min > 100) set_fault(h, ch, 5, t);
        }
    }
    if (aht_age_ms < 10000u && aht_t >= CHAMBER_FAULT) set_fault(h, ch, 4, t);
    if (ch->running && aht_age_ms >= 5000u) set_fault(h, ch, 1, t);

    /* ---- control ---- */
    uint8_t power = 0;
    if (ch->running && !h->fault) {
        int32_t e = (int32_t)ch->setpoint * 100 - aht_t;   /* 0.01 K */
        if (e > 500) {
            power = 100;                                   /* far below the setpoint: full power */
            h->in_pi = 0;
        } else {
            /* tuned with a 15 min control test at 45 °C (steady-state power 13-15 %) */
            if (!h->in_pi) {                               /* entering: preload I with the estimated steady power */
                int32_t v = ((int32_t)ch->setpoint - 25) * 70;   /* 0.7 % per K above 25 °C, in 0.01 % */
                h->i_term = v < 0 ? 0 : v;
                h->i_time = t;
                h->in_pi = 1;
            }
            if (t - h->i_time >= 1000u) {                  /* once per second: 0.03 % per K */
                h->i_time = t;
                if (e > -150) h->i_term += e * 3 / 100;
            }
            if (h->i_term < 0) h->i_term = 0;
            if (h->i_term > 5000) h->i_term = 5000;        /* at most 50 % from the integral term */
            int32_t p = e * 15 / 100;                      /* 15 % per K, also braking above the setpoint */
            int32_t l = p + h->i_term / 100;
            if (e < -150) l = 0;                           /* more than 1.5 K above the setpoint: off */
            if (l > 100) l = 100;
            if (l < 0) l = 0;
            power = (uint8_t)l;
        }
        /* heater surface limit: derate from 110 °C, off from 120 °C */
        if (h->ntc_t >= NTC_CONTROL_LIMIT) power = 0;
        else if (h->ntc_t > NTC_CONTROL_LIMIT - 100) {
            uint32_t limit = (uint32_t)(NTC_CONTROL_LIMIT - h->ntc_t);   /* 0..100 */
            if (power > limit) power = (uint8_t)limit;
        }
        /* fault 6: 3 min at more than 50 %, heater surface not at least 3 K warmer */
        if (power > 50) {
            if (!h->check_active) { h->check_active = 1; h->check_start = t; h->check_ntc = h->ntc_t; }
            else if (t - h->check_start >= 180000u) {
                if (h->ntc_t - h->check_ntc < 30) set_fault(h, ch, 6, t);
                h->check_active = 0;
            }
        } else h->check_active = 0;
    } else {
        h->i_term = 0; h->check_active = 0; h->in_pi = 0;
    }
    h->power = power;
    h->heat_request = power && (t % WINDOW_MS) < (uint32_t)power * (WINDOW_MS / 100u);

    /* ---- fan: always while drying, then after-run ---- */
    if (ch->running) { h->afterrun = 0; h->fan_request = 1; return; }
    if (!h->afterrun && (h->ntc_t > AFTERRUN_UNTIL_NTC || actually_heating)) { h->afterrun = 1; h->afterrun_start = t; }
    if (h->afterrun) {
        uint32_t d = t - h->afterrun_start;
        int cold = h->ntc_t != INVALID && h->ntc_t <= AFTERRUN_UNTIL_NTC;
        if (h->fault == 5 || d < AFTERRUN_MIN_MS || (!cold && d < AFTERRUN_MAX_MS)) h->fan_request = 1;
        else { h->afterrun = 0; h->fan_request = 0; }
    } else h->fan_request = 0;
}
