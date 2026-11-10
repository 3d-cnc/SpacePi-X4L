/* Custom firmware for the Creality SpacePi X4L (board Dryer_Pro_V14, GD32F303CBT6).
 *
 *   Display   PB10 = SCL, PB11 = SDA, address 0x7E (segment LCD, 20 bytes)
 *   PA6       high as in the original (display supply/backlight)
 *   Keys      PA2..PA5, active high
 *   AHT20     bus 1 (left): PB12 = SCL, PB13 = SDA   bus 2 (right): PB14 = SCL, PB15 = SDA
 *   NTC       PA0 left, PA1 right (ADC channel 0, 1)
 *   Heater    PA10 left, PA9 right - only ever on with the fan of the same chamber running
 *   Fan       PB0 left, PB1 right
 *   UART      PB6 = TX, PB7 = RX (USART0 remapped), 115200 8N1, protocol see protocol.h
 *
 * Status at 0x20000000, control at 0x20000100 - read/write over SWD (tools/x4l.py).
 */
#include <stdint.h>
#include "regs.h"
#include "display.h"
#include "ui.h"
#include "heater.h"
#include "protocol.h"

/* Which AHT20 bus belongs to which chamber (confirmed with a heat test):
 * bus 1 (PB12/PB13) = left, bus 2 (PB14/PB15) = right. */
#define AHT_LEFT  0
#define AHT_RIGHT 1

#define VERSION 0x00010301u   /* 1.3.1 */

/* Keys (bit in st.keys) */
#define K_LR      (1u << 0)   /* PA2 */
#define K_DOWN    (1u << 1)   /* PA3 */
#define K_UP      (1u << 2)   /* PA4 */
#define K_GEAR    (1u << 3)   /* PA5 */
#define PHOTO_STEPS 14        /* mode 8: 0 all on, 1..5 byte code, 6..13 bit plane */

void outputs_safe(void);

/* ---------- status and control (fixed RAM block) ---------- */
typedef struct {
    uint32_t magic;            /* 0x314C3458 = "X4L1" */
    uint32_t version;
    uint32_t ms;
    uint32_t clock_hz;
    uint32_t reset_cause;      /* RCC_CSR at start */
    uint32_t keys;             /* bit0..3 = PA2..PA5 */
    uint32_t key_count[4];
    int32_t  aht_t[2];         /* 0.01 °C */
    int32_t  aht_rh[2];        /* 0.01 % */
    uint32_t aht_ok[2];
    uint32_t aht_errors[2];
    uint32_t aht_status[2];    /* last status byte, 0xFFFFFFFF = no answer */
    uint32_t ntc_raw[2];       /* ADC 0..4095, mean of 16 */
    uint32_t lcd_frames;
    uint32_t lcd_nack;
    uint32_t fan_test;         /* active fan test, bit0 = PB0, bit1 = PB1 */
    uint32_t fan_test_left_ms;
    uint8_t  lcd_frame[20];    /* what is currently on the display */
    /* since 1.1.0: user interface (offset 124) */
    uint32_t view;             /* 0 left, 1 right */
    uint32_t field;            /* 0 none, 1 material, 2 temperature, 3 time */
    uint32_t running[2];
    uint32_t remaining_s[2];
    uint32_t setpoint[2];
    uint32_t duration_min[2];
    uint32_t material[2];
    /* since 1.2.0: heater (offset 172) */
    uint32_t fault[2];
    uint32_t power[2];         /* % */
    int32_t  ntc_t[2];         /* 0.1 °C heater surface, -32768 = invalid */
    uint32_t fan_on[2];
    uint32_t heater_on[2];
    /* since 1.3.0: UART protocol (offset 212) */
    uint32_t uart_commands;    /* executed commands */
    uint32_t uart_errors;      /* discarded/rejected lines */
} status_t;

typedef struct {
    uint32_t magic;            /* 0x52455453, set by the firmware */
    uint32_t mode;             /* 0 user interface, 9 sample frame, 1 all off, 2 all on,
                                  3 only byte n, 4 only bit n, 5 show raw[],
                                  6 bit plane n (bit 0x80>>n in every byte),
                                  7 byte code n (all bytes i with bit n set in i),
                                  8 photo series step n (see PHOTO_STEPS) */
    uint32_t n;
    uint32_t fan_test;         /* bit0 = PB0, bit1 = PB1; cleared after 2 s */
    uint8_t  raw[20];
    uint32_t command;          /* offset 0x24: 1 = stop all chambers */
} control_t;

__attribute__((section(".status"))) volatile status_t st;
__attribute__((section(".control"))) volatile control_t sg;

/* Sample frame for test mode 9 (idle: 04:00:00, 26 °C, 40 %, setpoint 75 °C, ABS) */
static const uint8_t SAMPLE_FRAME[20] = {
    0xe0, 0xbd, 0xbb, 0xdf, 0xb6, 0xbf, 0xbf, 0xbf, 0xff, 0x6d,
    0xfb, 0xd6, 0xbf, 0xbb, 0x04, 0x05, 0x04, 0xef, 0xff, 0xbd,
};

/* ---------- time ---------- */
static volatile uint32_t ms_counter;
static uint32_t clock_hz = 8000000;

void SysTick_Handler(void) { ms_counter++; }

static uint32_t now(void) { return ms_counter; }

static void wait_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT, duration = us * (clock_hz / 1000000u);
    while (DWT_CYCCNT - start < duration) ;
}

static void wait_ms(uint32_t ms)
{
    uint32_t start = now();
    while (now() - start < ms) IWDG_KR = 0xAAAA;
}

/* ---------- clock: 8 MHz crystal x 9 = 72 MHz (works on GD32F303 and STM32F103) ---------- */
static void clock_init(void)
{
    RCC_CR |= 1u << 16;                                   /* HSE on */
    uint32_t i;
    for (i = 0; i < 2000000u && !(RCC_CR & (1u << 17)); i++) ;
    if (RCC_CR & (1u << 17)) {
        FLASH_ACR = 0x12;                                 /* prefetch, 2 wait states */
        RCC_CFGR = (7u << 18) | (1u << 16) | (4u << 8) | (2u << 14);  /* PLL x9 from HSE, APB1/2, ADC/6 */
        RCC_CR |= 1u << 24;
        while (!(RCC_CR & (1u << 25))) ;
        RCC_CFGR |= 2u;
        while (((RCC_CFGR >> 2) & 3u) != 2u) ;
        clock_hz = 72000000u;
    }
    /* otherwise continue on the internal 8 MHz oscillator */
    SYST_RVR = clock_hz / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7;                                         /* core clock, interrupt, enable */
    DEMCR |= 1u << 24;                                    /* DWT for microseconds */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
}

/* ---------- GPIO ---------- */
static void pin_mode(uint32_t port, uint32_t pin, uint32_t mode)
{
    volatile uint32_t *cr = pin < 8 ? &GPIO_CRL(port) : &GPIO_CRH(port);
    uint32_t s = (pin & 7u) * 4u;
    *cr = (*cr & ~(0xFu << s)) | (mode << s);
}

static void pin_set(uint32_t port, uint32_t pin, int high)
{
    if (high) GPIO_BSRR(port) = 1u << pin; else GPIO_BRR(port) = 1u << pin;
}

static int pin_read(uint32_t port, uint32_t pin) { return (GPIO_IDR(port) >> pin) & 1u; }

static void pins_init(void)
{
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 9) | (1u << 14); /* AFIO, A, B, ADC0, USART0 */
    outputs_safe();
    pin_mode(GPIOA, 0, M_ANALOG);
    pin_mode(GPIOA, 1, M_ANALOG);
    for (uint32_t p = 2; p <= 5; p++) { pin_set(GPIOA, p, 0); pin_mode(GPIOA, p, M_PULL); }  /* pull-down */
    pin_set(GPIOA, 6, 1);  pin_mode(GPIOA, 6, M_OUT_PP);
    pin_set(GPIOB, 10, 1); pin_mode(GPIOB, 10, M_OUT_PP);
    pin_set(GPIOB, 11, 1); pin_mode(GPIOB, 11, M_OUT_PP);
    for (uint32_t p = 12; p <= 15; p++) { pin_set(GPIOB, p, 1); pin_mode(GPIOB, p, M_PULL); } /* released, pull-up */
    AFIO_MAPR |= 1u << 2;                                 /* USART0 -> PB6/PB7 */
    pin_mode(GPIOB, 6, M_AF_PP);
    pin_set(GPIOB, 7, 1); pin_mode(GPIOB, 7, M_PULL);
}

/* ---------- display: software I2C on PB10 (SCL) / PB11 (SDA) ---------- */
#define LCD_T 10   /* half clock period in us -> about 50 kHz */

static void lcd_scl(int h) { pin_set(GPIOB, 10, h); }
static void lcd_sda(int h) { pin_set(GPIOB, 11, h); }

static void lcd_start(void)
{
    lcd_sda(1); lcd_scl(1); wait_us(LCD_T);
    lcd_sda(0); wait_us(LCD_T);
    lcd_scl(0); wait_us(LCD_T);
}

static void lcd_stop(void)
{
    lcd_sda(0); wait_us(LCD_T);
    lcd_scl(1); wait_us(LCD_T);
    lcd_sda(1); wait_us(LCD_T);
}

static int lcd_byte(uint8_t b)   /* 1 = acknowledged */
{
    for (int i = 7; i >= 0; i--) {
        lcd_sda((b >> i) & 1);
        wait_us(LCD_T / 2);
        lcd_scl(1); wait_us(LCD_T);
        lcd_scl(0); wait_us(LCD_T / 2);
    }
    /* release SDA for the acknowledge (input with pull-up) instead of driving
     * against the display driver */
    pin_set(GPIOB, 11, 1); pin_mode(GPIOB, 11, M_PULL);
    wait_us(LCD_T / 2);
    lcd_scl(1); wait_us(LCD_T / 2);
    int ack = !pin_read(GPIOB, 11);
    wait_us(LCD_T / 2);
    lcd_scl(0);
    pin_set(GPIOB, 11, 0); pin_mode(GPIOB, 11, M_OUT_PP);
    wait_us(LCD_T / 2);
    if (!ack) st.lcd_nack++;
    return ack;
}

static void lcd_command(uint8_t b)
{
    lcd_start();
    if (lcd_byte(0x7E)) lcd_byte(b);
    lcd_stop();
}

static void lcd_send_frame(const uint8_t *frame)
{
    lcd_start();
    if (lcd_byte(0x7E) && lcd_byte(0x00)) {
        for (int i = 0; i < 20; i++) if (!lcd_byte(frame[i])) break;
    }
    lcd_stop();
    for (int i = 0; i < 20; i++) st.lcd_frame[i] = frame[i];
    st.lcd_frames++;
}

static void lcd_init(void)
{
    wait_ms(5);
    lcd_command(0x9C);   /* init sequence of the display driver */
    lcd_command(0xC0);
    lcd_command(0x72);
}

/* ---------- AHT20: software I2C, pseudo open-drain with internal pull-up ---------- */
#define AHT_T 5
static const uint8_t AHT_SCL[2] = { 12, 14 };
static const uint8_t AHT_SDA[2] = { 13, 15 };

static void od(uint32_t pin, int high)
{
    if (high) { pin_mode(GPIOB, pin, M_PULL); pin_set(GPIOB, pin, 1); }
    else      { pin_set(GPIOB, pin, 0); pin_mode(GPIOB, pin, M_OUT_OD); }
}

static void a_start(int k) { od(AHT_SDA[k], 1); od(AHT_SCL[k], 1); wait_us(AHT_T); od(AHT_SDA[k], 0); wait_us(AHT_T); od(AHT_SCL[k], 0); wait_us(AHT_T); }
static void a_stop(int k)  { od(AHT_SDA[k], 0); wait_us(AHT_T); od(AHT_SCL[k], 1); wait_us(AHT_T); od(AHT_SDA[k], 1); wait_us(AHT_T); }

static int a_write(int k, uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        od(AHT_SDA[k], (b >> i) & 1); wait_us(AHT_T);
        od(AHT_SCL[k], 1); wait_us(AHT_T);
        od(AHT_SCL[k], 0);
    }
    od(AHT_SDA[k], 1); wait_us(AHT_T);
    od(AHT_SCL[k], 1); wait_us(AHT_T);
    int ack = !pin_read(GPIOB, AHT_SDA[k]);
    od(AHT_SCL[k], 0); wait_us(AHT_T);
    return ack;
}

static uint8_t a_read(int k, int acknowledge)
{
    uint8_t b = 0;
    od(AHT_SDA[k], 1);
    for (int i = 0; i < 8; i++) {
        wait_us(AHT_T); od(AHT_SCL[k], 1); wait_us(AHT_T);
        b = (uint8_t)(b << 1) | (uint8_t)pin_read(GPIOB, AHT_SDA[k]);
        od(AHT_SCL[k], 0);
    }
    od(AHT_SDA[k], acknowledge ? 0 : 1); wait_us(AHT_T);
    od(AHT_SCL[k], 1); wait_us(AHT_T);
    od(AHT_SCL[k], 0); od(AHT_SDA[k], 1); wait_us(AHT_T);
    return b;
}

static int aht_command(int k, uint8_t a, uint8_t b, uint8_t c)
{
    a_start(k);
    int ok = a_write(k, 0x70) && a_write(k, a) && a_write(k, b) && a_write(k, c);
    a_stop(k);
    return ok;
}

static int aht_read(int k, uint8_t *d, int n)
{
    a_start(k);
    if (!a_write(k, 0x71)) { a_stop(k); return 0; }
    for (int i = 0; i < n; i++) d[i] = a_read(k, i < n - 1);
    a_stop(k);
    return 1;
}

static uint8_t crc8(const uint8_t *d, int n)
{
    uint8_t c = 0xFF;
    while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1); }
    return c;
}

/* State machine per sensor: 0 check/calibrate, 1 start measurement, 2 fetch result */
static uint8_t aht_state[2];
static uint32_t aht_time[2];

static void aht_step(int k)
{
    uint32_t t = now();
    uint8_t d[7];
    switch (aht_state[k]) {
    case 0:
        if ((int32_t)(t - aht_time[k]) < 100) return;   /* signed: aht_time may lie in the future */
        if (!aht_read(k, d, 1)) { st.aht_status[k] = 0xFFFFFFFFu; st.aht_errors[k]++; aht_time[k] = t + 900; return; }
        st.aht_status[k] = d[0];
        if (!(d[0] & 0x08)) { aht_command(k, 0xBE, 0x08, 0x00); aht_time[k] = t; return; }
        aht_state[k] = 1;
        /* fall through */
    case 1:
        if ((int32_t)(t - aht_time[k]) < 0) return;
        if (!aht_command(k, 0xAC, 0x33, 0x00)) { st.aht_errors[k]++; st.aht_status[k] = 0xFFFFFFFFu; aht_state[k] = 0; aht_time[k] = t + 900; return; }
        aht_time[k] = t; aht_state[k] = 2;
        return;
    case 2:
        if (t - aht_time[k] < 90) return;
        if (!aht_read(k, d, 7)) { st.aht_errors[k]++; aht_state[k] = 0; aht_time[k] = t + 900; return; }
        st.aht_status[k] = d[0];
        if (d[0] & 0x80) { if (t - aht_time[k] > 300) { st.aht_errors[k]++; aht_state[k] = 1; aht_time[k] = t + 700; } return; }
        if (crc8(d, 6) != d[6]) { st.aht_errors[k]++; aht_state[k] = 1; aht_time[k] = t + 900; return; }
        {
            uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
            uint32_t tt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
            st.aht_rh[k] = (int32_t)(((uint64_t)rh * 10000u) >> 20);
            st.aht_t[k]  = (int32_t)(((uint64_t)tt * 20000u) >> 20) - 5000;
            st.aht_ok[k]++;
        }
        aht_state[k] = 1; aht_time[k] = t + 900;   /* again every second */
        return;
    }
}

/* ---------- NTC via ADC ---------- */
static void adc_init(void)
{
    ADC_CR2 = 1u;                       /* power on */
    wait_ms(2);
    ADC_CR2 |= 1u << 3; while (ADC_CR2 & (1u << 3)) ;   /* reset calibration */
    ADC_CR2 |= 1u << 2; while (ADC_CR2 & (1u << 2)) ;   /* calibrate */
    ADC_SMPR2 = 7u | (7u << 3);         /* channels 0 and 1: longest sample time */
    ADC_SQR1 = 0;                       /* one conversion */
    ADC_CR2 |= (7u << 17) | (1u << 20); /* software trigger */
}

static uint32_t adc_convert(uint32_t channel)
{
    ADC_SQR3 = channel;
    ADC_SR = 0;
    ADC_CR2 |= 1u << 22;
    uint32_t t = now();
    while (!(ADC_SR & 2u)) if (now() - t > 5) return 0xFFFF;
    return ADC_DR & 0xFFFu;
}

/* ---------- UART ---------- */
static void uart_init(void)
{
    USART_BRR = clock_hz / 115200u;
    USART_CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);   /* UE, RXNEIE, TE, RE */
    NVIC_ISER1 = 1u << (USART0_IRQ - 32);
}

/* ---------- watchdog: about 2 s ---------- */
static void watchdog_start(void)
{
    IWDG_KR = 0x5555; IWDG_PR = 4; IWDG_RLR = 1250; IWDG_KR = 0xAAAA; IWDG_KR = 0xCCCC;
}

/* ---------- main program ---------- */
int main(void)
{
    uint32_t reset_cause = RCC_CSR;
    RCC_CSR |= 1u << 24;                /* clear reset flags */
    pins_init();
    clock_init();
    watchdog_start();

    for (unsigned i = 0; i < sizeof(st); i++) ((volatile uint8_t *)&st)[i] = 0;
    st.magic = 0x314C3458u; st.version = VERSION; st.clock_hz = clock_hz; st.reset_cause = reset_cause;
    for (unsigned i = 0; i < sizeof(sg); i++) ((volatile uint8_t *)&sg)[i] = 0;
    sg.magic = 0x52455453u;

    uart_init();
    adc_init();
    lcd_init();
    ui_init();
    protocol_send_version(VERSION);

    uint32_t t_lcd = 0, t_ntc = 0, t_uart = 0, t_keys = 0, version_counter = 0;
    uint32_t old_mode = 0xFFFFFFFFu, old_n = 0xFFFFFFFFu, old_fan_test = 0, fan_test_start = 0;
    uint32_t ntc_sum[2] = { 0, 0 }, ntc_count = 0;
    uint32_t aht_ok_old[2] = { 0, 0 }, aht_time_ok[2] = { 0, 0 }, fan_since[2] = { 0, 0 };
    uint8_t fan_running[2] = { 0, 0 };
    uint8_t frame[20];

    for (;;) {
        IWDG_KR = 0xAAAA;
        uint32_t t = now();
        st.ms = t;

        /* keys (debounced by the 20 ms grid) */
        if (t - t_keys >= 20) {
            t_keys = t;
            uint32_t keys = (GPIO_IDR(GPIOA) >> 2) & 0xFu;
            uint32_t pressed = keys & ~st.keys;
            for (int i = 0; i < 4; i++) if (pressed & (1u << i)) st.key_count[i]++;
            st.keys = keys;
            if (sg.mode == 0u) {
                if ((pressed & K_GEAR) && heater[view].fault) heater_acknowledge(view);
                ui_keys(keys, t);
            } else if (sg.mode == 8u) {           /* photo series (started over SWD only) */
                if (pressed & K_GEAR) sg.mode = 0;
                if ((pressed & K_UP) && sg.n + 1u < PHOTO_STEPS) sg.n = sg.n + 1u;
                if ((pressed & K_DOWN) && sg.n > 0u) sg.n = sg.n - 1u;
            }
        }
        ui_tick(t);
        st.view = view; st.field = field;
        for (int k = 0; k < 2; k++) {
            st.running[k] = chamber[k].running; st.remaining_s[k] = chamber[k].remaining_s; st.setpoint[k] = chamber[k].setpoint;
            st.duration_min[k] = chamber[k].duration_min; st.material[k] = chamber[k].material;
        }

        /* SWD command 1 = stop all chambers immediately */
        if (sg.command == 1u) { chamber[0].running = chamber[1].running = 0; sg.command = 0; }

        /* age of the chamber readings */
        for (int b = 0; b < 2; b++) if (st.aht_ok[b] != aht_ok_old[b]) { aht_ok_old[b] = st.aht_ok[b]; aht_time_ok[b] = t; }

        /* fan test on PB0/PB1 over SWD, at most 2 s */
        uint32_t ft = sg.fan_test & 3u;
        if (ft != old_fan_test) { old_fan_test = ft; fan_test_start = t; }
        if (ft && t - fan_test_start >= 2000u) { sg.fan_test = 0; ft = 0; old_fan_test = 0; }
        st.fan_test = ft;
        st.fan_test_left_ms = ft ? 2000u - (t - fan_test_start) : 0;

        /* ---------- heater and fan per chamber ----------
         * chamber 0 = left:  heater PA10, fan PB0, NTC PA0, AHT bus AHT_LEFT
         * chamber 1 = right: heater PA9,  fan PB1, NTC PA1, AHT bus AHT_RIGHT */
        static const uint8_t HEATER_PIN[2] = { 10, 9 }, FAN_PIN[2] = { 0, 1 }, AHT_BUS[2] = { AHT_LEFT, AHT_RIGHT };
        for (int k = 0; k < 2; k++) {
            if (chamber[k].running && heater[k].fault) chamber[k].running = 0;   /* no start with a fault */
            int b = AHT_BUS[k];
            uint32_t age = st.aht_ok[b] ? t - aht_time_ok[b] : 0xFFFFFFFFu;
            uint8_t heating = (uint8_t)((GPIO_ODR(GPIOA) >> HEATER_PIN[k]) & 1u);
            heater_step(k, t, &chamber[k], st.aht_t[b], age, st.ntc_raw[k], heating);

            /* fan first */
            int fan = heater[k].fan_request || ((ft >> k) & 1u);
            pin_set(GPIOB, FAN_PIN[k], fan);
            int fan_on = (int)((GPIO_ODR(GPIOB) >> FAN_PIN[k]) & 1u);
            /* a flag instead of "t | 1u": that could lie 1 ms in the future, the difference
             * overflowed and enabled the heater in the first tick (bug up to 1.2.1) */
            if (!fan_on) fan_running[k] = 0;
            else if (!fan_running[k]) { fan_running[k] = 1; fan_since[k] = t; }

            /* heater only with the fan running (for >= 3 s) - ground rule of this project */
            int allowed = heater[k].heat_request && !heater[k].fault && chamber[k].running
                          && fan_on && fan_running[k] && t - fan_since[k] >= FAN_LEAD_MS;
            pin_set(GPIOA, HEATER_PIN[k], allowed);

            st.fault[k] = heater[k].fault; st.power[k] = heater[k].power;
            st.ntc_t[k] = heater[k].ntc_t; st.fan_on[k] = (uint32_t)fan_on; st.heater_on[k] = (uint32_t)allowed;
        }

        /* display: user interface every 100 ms, test modes on change or every 500 ms */
        if (sg.mode == 0u) {
            if (t - t_lcd >= 100) {
                readings_t mw;
                static const int bus[2] = { AHT_LEFT, AHT_RIGHT };
                for (int k = 0; k < 2; k++) {
                    mw.t[k] = st.aht_t[bus[k]]; mw.rh[k] = st.aht_rh[bus[k]];
                    mw.ok[k] = st.aht_ok[bus[k]] != 0 && st.aht_status[bus[k]] != 0xFFFFFFFFu;
                    mw.fault[k] = heater[k].fault; mw.heating[k] = heater[k].power > 0;
                }
                ui_frame(frame, &mw, t);
                lcd_send_frame(frame);
                t_lcd = t;
            }
            old_mode = 0;
        } else if (sg.mode != old_mode || sg.n != old_n || sg.mode == 5u || t - t_lcd >= 500) {
            uint32_t m = sg.mode, n = sg.n;
            if (m == 8u) {                          /* map the photo series onto modes 2/7/6 */
                if (n == 0u) m = 2;
                else if (n <= 5u) { m = 7; n = n - 1u; }
                else { m = 6; n = n - 6u; }
            }
            for (int i = 0; i < 20; i++) {
                switch (m) {
                case 6: frame[i] = (uint8_t)(0x80u >> (n % 8u)); break;
                case 7: frame[i] = ((uint32_t)i >> (n % 5u)) & 1u ? 0xFF : 0x00; break;
                case 9: frame[i] = SAMPLE_FRAME[i]; break;
                case 1: frame[i] = 0x00; break;
                case 2: frame[i] = 0xFF; break;
                case 3: frame[i] = (n % 20u == (uint32_t)i) ? 0xFF : 0x00; break;
                case 4: frame[i] = (n % 160u) / 8u == (uint32_t)i ? (uint8_t)(0x80u >> (n % 8u)) : 0x00; break;
                default: frame[i] = sg.raw[i]; break;
                }
            }
            if (m != 5u || t - t_lcd >= 100) { lcd_send_frame(frame); t_lcd = t; }
            old_mode = sg.mode; old_n = sg.n;
        }

        /* NTC: sample every 10 ms, average every 160 ms */
        if (t - t_ntc >= 10) {
            t_ntc = t;
            ntc_sum[0] += adc_convert(0);
            ntc_sum[1] += adc_convert(1);
            if (++ntc_count == 16) {
                st.ntc_raw[0] = ntc_sum[0] / 16u; st.ntc_raw[1] = ntc_sum[1] / 16u;
                ntc_sum[0] = ntc_sum[1] = 0; ntc_count = 0;
            }
        }

        aht_step(0);
        aht_step(1);

        /* protocol to the ESP32: process commands, status every second, version every 10 s */
        protocol_commands(t);
        if (t - t_uart >= 1000) {
            t_uart = t;
            chamber_status_t cs[2];
            static const uint8_t BUS[2] = { AHT_LEFT, AHT_RIGHT };
            for (int k = 0; k < 2; k++) {
                int b = BUS[k];
                cs[k].running = chamber[k].running; cs[k].setpoint = chamber[k].setpoint; cs[k].material = chamber[k].material;
                cs[k].duration_min = chamber[k].duration_min; cs[k].remaining_s = chamber[k].remaining_s;
                cs[k].fault = heater[k].fault; cs[k].power = heater[k].power;
                cs[k].fan = (uint8_t)st.fan_on[k];
                cs[k].sensor_ok = st.aht_ok[b] != 0 && st.aht_status[b] != 0xFFFFFFFFu;
                cs[k].t = st.aht_t[b]; cs[k].rh = st.aht_rh[b]; cs[k].ntc = heater[k].ntc_t;
            }
            protocol_send_status(cs);
            if (++version_counter >= 10) { version_counter = 0; protocol_send_version(VERSION); }
            st.uart_commands = protocol_commands_total(); st.uart_errors = protocol_errors_total();
        }
    }
}
