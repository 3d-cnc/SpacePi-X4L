#include "ui.h"
#include "display.h"
#include "settings.h"
#include "heater.h"

#define K_LR    (1u << 0)
#define K_DOWN  (1u << 1)
#define K_UP    (1u << 2)
#define K_GEAR  (1u << 3)

#define SETPOINT_MIN 35
#define SETPOINT_MAX 75          /* manufacturer limit of the X4L */
#define DURATION_MIN 30          /* minutes */
#define DURATION_MAX (48 * 60)
#define LONG_PRESS_MS 1500
#define FIELD_TIMEOUT_MS 8000

typedef struct { char name[7]; uint8_t setpoint; uint16_t duration_min; } material_t;
static const material_t MATERIAL[] = {
    { "PLA",  50, 360 },
    { "PEtG", 65, 360 },
    { "AbS",  75, 360 },
    { "ASA",  75, 360 },
    { "tPU",  55, 480 },
    { "PA",   75, 720 },
    { "PC",   75, 480 },
    { "USEr", 50, 240 },
};
#define MATERIAL_COUNT (sizeof(MATERIAL) / sizeof(MATERIAL[0]))

chamber_t chamber[2];
uint8_t view, field;

static uint32_t old_keys, press_start[4], repeat_at, last_action, second;
static uint8_t long_done, save_pending;

static void material_apply(chamber_t *k, uint8_t m)
{
    k->material = m;
    k->setpoint = MATERIAL[m].setpoint;
    k->duration_min = MATERIAL[m].duration_min;
}

static void save(void)
{
    settings_t s;
    for (int i = 0; i < 2; i++) {
        s.material[i] = chamber[i].material;
        s.setpoint[i] = chamber[i].setpoint;
        s.duration_min[i] = chamber[i].duration_min;
    }
    settings_save(&s);
    save_pending = 0;
}

void ui_init(void)
{
    settings_t s;
    int ok = settings_load(&s);
    for (int i = 0; i < 2; i++) {
        chamber_t *k = &chamber[i];
        if (ok && s.material[i] < MATERIAL_COUNT && s.setpoint[i] >= SETPOINT_MIN && s.setpoint[i] <= SETPOINT_MAX
            && s.duration_min[i] >= DURATION_MIN && s.duration_min[i] <= DURATION_MAX) {
            k->material = s.material[i]; k->setpoint = s.setpoint[i]; k->duration_min = s.duration_min[i];
        } else {
            material_apply(k, 0);
        }
        k->running = 0; k->remaining_s = 0; k->total_s = 0;
    }
    view = 0; field = 0;
}

static void start_stop(chamber_t *k)
{
    if (k->running) { k->running = 0; k->remaining_s = 0; return; }
    k->total_s = (uint32_t)k->duration_min * 60u;
    k->remaining_s = k->total_s;
    k->running = 1;
}

static void change_value(int direction)
{
    chamber_t *k = &chamber[view];
    switch (field) {
    case 1:
        if (k->running) return;
        material_apply(k, (uint8_t)((k->material + MATERIAL_COUNT + (unsigned)direction) % MATERIAL_COUNT));
        break;
    case 2: {
        int s = k->setpoint + direction;
        if (s < SETPOINT_MIN) s = SETPOINT_MIN;
        if (s > SETPOINT_MAX) s = SETPOINT_MAX;
        k->setpoint = (uint8_t)s;
        break;
    }
    case 3: {
        if (k->running) return;
        int d = k->duration_min + direction * 30;
        if (d < DURATION_MIN) d = DURATION_MIN;
        if (d > DURATION_MAX) d = DURATION_MAX;
        k->duration_min = (uint16_t)d;
        break;
    }
    default:
        return;
    }
    save_pending = 1;
}

void ui_keys(uint32_t keys, uint32_t t)
{
    uint32_t pressed = keys & ~old_keys, released = old_keys & ~keys;
    for (int i = 0; i < 4; i++) if (pressed & (1u << i)) press_start[i] = t;
    if (pressed) last_action = t;

    if (pressed & K_LR) {
        view ^= 1u; field = 0;
        if (save_pending) save();
    }

    /* gear: short = next field, long = start/stop */
    if (pressed & K_GEAR) long_done = 0;
    if ((keys & K_GEAR) && !long_done && t - press_start[3] >= LONG_PRESS_MS) {
        long_done = 1; field = 0;
        if (save_pending) save();
        start_stop(&chamber[view]);
    }
    if ((released & K_GEAR) && !long_done) {
        field = (uint8_t)((field + 1u) % 4u);
        if (chamber[view].running && (field == 1 || field == 3)) field = field == 1 ? 2 : 0;   /* while running only the temperature */
        if (field == 0 && save_pending) save();
    }

    /* arrows with auto-repeat */
    if (pressed & K_UP) { change_value(+1); repeat_at = t + 600; }
    if (pressed & K_DOWN) { change_value(-1); repeat_at = t + 600; }
    if ((keys & (K_UP | K_DOWN)) && !(pressed & (K_UP | K_DOWN)) && (int32_t)(t - repeat_at) >= 0) {
        change_value((keys & K_UP) ? +1 : -1);
        repeat_at = t + 120;
        last_action = t;
    }
    old_keys = keys;
}

/* ---------- remote control ---------- */
static uint32_t remote_change;

int material_count(void) { return (int)MATERIAL_COUNT; }

int remote_start(int k, uint32_t t)
{
    (void)t;
    if (chamber[k].running) return 0;
    if (heater[k].fault) return 2;
    start_stop(&chamber[k]);
    return 0;
}

int remote_stop(int k, uint32_t t)
{
    (void)t;
    if (chamber[k].running) start_stop(&chamber[k]);
    return 0;
}

int remote_setpoint(int k, int degrees, uint32_t t)
{
    if (degrees < SETPOINT_MIN || degrees > SETPOINT_MAX) return 1;
    chamber[k].setpoint = (uint8_t)degrees;
    save_pending = 1; remote_change = t;
    return 0;
}

int remote_duration(int k, int minutes, uint32_t t)
{
    if (minutes < DURATION_MIN || minutes > DURATION_MAX) return 1;
    if (chamber[k].running) return 3;
    chamber[k].duration_min = (uint16_t)minutes;
    save_pending = 1; remote_change = t;
    return 0;
}

int remote_material(int k, int index, uint32_t t)
{
    if (index < 0 || index >= (int)MATERIAL_COUNT) return 1;
    if (chamber[k].running) return 3;
    material_apply(&chamber[k], (uint8_t)index);
    save_pending = 1; remote_change = t;
    return 0;
}

void ui_tick(uint32_t t)
{
    if (field && t - last_action >= FIELD_TIMEOUT_MS) {
        field = 0;
        if (save_pending) save();
    }
    if (!field && save_pending && t - remote_change >= 10000u) save();   /* spare the flash */
    if (t - second >= 1000) {
        second += 1000;
        if (t - second >= 1000) second = t;             /* do not catch up after a long pause */
        for (int i = 0; i < 2; i++) {
            chamber_t *k = &chamber[i];
            if (k->running && k->remaining_s && --k->remaining_s == 0) k->running = 0;
        }
    }
}

static void two_digits(uint8_t *b, int tens, int ones, int value, uint16_t hundred)
{
    if (value >= 100) { frame_symbol(b, hundred, 1); value -= 100; }
    if (value >= 10) frame_digit(b, tens, (char)('0' + value / 10));
    frame_digit(b, ones, (char)('0' + value % 10));
}

void ui_frame(uint8_t *b, const readings_t *m, uint32_t t)
{
    const chamber_t *k = &chamber[view];
    const chamber_t *other = &chamber[view ^ 1u];
    int blink_off = (t / 400u) & 1u;

    frame_clear(b);
    /* fixed symbols */
    frame_symbol(b, S_GEAR, 1); frame_symbol(b, S_ARROW_UP, 1);
    frame_symbol(b, S_ARROW_DOWN, 1); frame_symbol(b, S_LR_KEY, 1);
    frame_symbol(b, S_TIME, 1); frame_symbol(b, S_MATERIAL, 1);
    frame_symbol(b, S_THERMOMETER, !(m->heating[view] && blink_off)); frame_symbol(b, S_PV, 1);
    frame_symbol(b, S_DROPLET, 1); frame_symbol(b, S_RH, 1); frame_symbol(b, S_PERCENT, 1);

    /* chamber: the one shown is steady, the other one blinks while it is running */
    int l_on = view == 0 || (other->running && !blink_off);
    int r_on = view == 1 || (other->running && !blink_off);
    frame_symbol(b, S_L, l_on);
    frame_symbol(b, S_R, r_on);

    /* setpoint (small) */
    if (!(field == 2 && blink_off)) {
        two_digits(b, D_SMALL_10, D_SMALL_1, k->setpoint, S_SMALL_HUNDRED);
        frame_symbol(b, S_SMALL_DEGREE, 1); frame_symbol(b, S_SMALL_C_BOTTOM, 1);
    }

    /* actual values */
    int ch = view;
    if (m->ok[ch] && m->t[ch] >= 0) {
        two_digits(b, D_BIG_10, D_BIG_1, (int)((m->t[ch] + 50) / 100), S_BIG_HUNDRED);
    } else {
        frame_digit(b, D_BIG_10, '-'); frame_digit(b, D_BIG_1, '-');
    }
    frame_symbol(b, S_BIG_DEGREE, 1); frame_symbol(b, S_BIG_C_BOTTOM, 1);
    if (m->ok[ch]) {
        int rh = (int)((m->rh[ch] + 50) / 100);
        if (rh > 99) rh = 99;
        if (rh < 0) rh = 0;
        two_digits(b, D_HUMIDITY_10, D_HUMIDITY_1, rh, S_PERCENT);
    } else {
        frame_digit(b, D_HUMIDITY_10, '-'); frame_digit(b, D_HUMIDITY_1, '-');
    }

    /* time: running = remaining time, otherwise the configured duration */
    if (!(field == 3 && blink_off)) {
        uint32_t s = k->running ? k->remaining_s : (uint32_t)k->duration_min * 60u;
        uint32_t h = s / 3600u, mi = (s / 60u) % 60u, se = s % 60u;
        if (h > 99) h = 99;
        char z[7] = { (char)('0' + h / 10), (char)('0' + h % 10), (char)('0' + mi / 10),
                      (char)('0' + mi % 10), (char)('0' + se / 10), (char)('0' + se % 10), 0 };
        frame_time(b, z);
        frame_symbol(b, S_COLON1, 1);
        frame_symbol(b, S_COLON2, !k->running || (k->remaining_s & 1u));
    }

    /* material, or blinking "Err n" on a heater fault */
    if (m->fault[view]) {
        char err[7] = { 'E', 'r', 'r', ' ', (char)('0' + m->fault[view] % 10u), 0, 0 };
        if (!blink_off) frame_text(b, D_MATERIAL, 6, err);
    } else if (!(field == 1 && blink_off)) frame_text(b, D_MATERIAL, 6, MATERIAL[k->material].name);

    /* state */
    frame_symbol(b, S_ON, k->running);
    frame_symbol(b, S_OFF, !k->running);

    /* ring: idle = full, while drying the remaining share clockwise */
    static const uint16_t RING[7] = { S_RING_BOTTOM_LEFT, S_RING_LEFT, S_RING_TOP_LEFT, S_RING_TOP,
                                      S_RING_TOP_RIGHT, S_RING_RIGHT, S_RING_BOTTOM_RIGHT };
    uint32_t n = 7;
    if (k->running && k->total_s) n = (k->remaining_s * 7u + k->total_s - 1u) / k->total_s;
    for (uint32_t i = 0; i < n && i < 7; i++) frame_symbol(b, RING[i], 1);
}
