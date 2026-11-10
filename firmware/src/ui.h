/* User interface: two chambers, four keys, segment display.
 *
 *   L/R              switch view (left / right chamber)
 *   gear short       select field: material -> setpoint -> time -> done
 *   gear 1.5 s       start / stop drying of the chamber shown
 *   arrows           change the selected value (hold = fast)
 *   8 s without key  leave the field, save settings
 */
#ifndef UI_H
#define UI_H
#include <stdint.h>

typedef struct {
    int32_t t[2];        /* 0.01 °C, [0] = left, [1] = right */
    int32_t rh[2];       /* 0.01 % */
    uint8_t ok[2];       /* 1 = reading valid */
    uint8_t fault[2];    /* heater fault 0..6 */
    uint8_t heating[2];  /* 1 = heater currently working (power > 0) */
} readings_t;

typedef struct {
    uint8_t  material;
    uint8_t  setpoint;      /* °C */
    uint16_t duration_min;
    uint8_t  running;       /* drying active */
    uint32_t remaining_s;
    uint32_t total_s;
} chamber_t;

extern chamber_t chamber[2];
extern uint8_t view;    /* 0 = left, 1 = right */
extern uint8_t field;   /* 0 none, 1 material, 2 temperature, 3 time */

void ui_init(void);
void ui_keys(uint32_t keys, uint32_t t);      /* every 20 ms, bit0 L/R, 1 down, 2 up, 3 gear */
void ui_tick(uint32_t t);                     /* every loop */
void ui_frame(uint8_t *frame, const readings_t *m, uint32_t t);

/* Remote control (UART / Home Assistant). Returns 0 = ok, 1 = invalid value,
 * 2 = chamber has a fault, 3 = not possible while the chamber is running.
 * Changed settings are saved 10 s after the last change. */
int remote_start(int k, uint32_t t);
int remote_stop(int k, uint32_t t);
int remote_setpoint(int k, int degrees, uint32_t t);
int remote_duration(int k, int minutes, uint32_t t);
int remote_material(int k, int index, uint32_t t);
int material_count(void);

#endif
