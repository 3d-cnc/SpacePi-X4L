/* Eigene Firmware fuer den Creality SpacePi X4L (Platine Dryer_Pro_V14, GD32F303CBT6).
 *
 * STUFE 1 - Grundlage und Test. Es gibt KEINEN Code fuer die Heizungen: PA9/PA10 sind
 * fest auf Low. (10.11.: PA9 schaltet nachweislich eine Heizung - Fuehler an PA1 wurde warm.)
 * Luefter (PB0 links, PB1 rechts) nur als zeitbegrenzter Test (max. 2 s).
 *
 *   Display   PB10 = SCL, PB11 = SDA, Adresse 0x7E (Segment-LCD, 20 Byte)
 *   PA6       High wie im Original (Display-Versorgung/Hintergrundlicht)
 *   Tasten    PA2..PA5, aktiv High
 *   AHT20     Bus 1: PB12 = SCL, PB13 = SDA   Bus 2: PB14 = SCL, PB15 = SDA
 *   NTC       PA0, PA1 (ADC Kanal 0, 1)
 *   Heizung   PA9 (Fuehler PA1), PA10 (vermutlich, Fuehler PA0) - IMMER AUS
 *   Luefter   PB0 = links, PB1 = rechts (10.11. bestaetigt), nur Test
 *   UART      PB6 = TX, PB7 = RX (USART0 umgeleitet), 115200 8N1
 *
 * Status bei 0x20000000, Steuerung bei 0x20000100 - per SWD lesen/schreiben
 * (werkzeuge/x4l.py).
 */
#include <stdint.h>
#include "regs.h"
#include "anzeige.h"
#include "bedienung.h"
#include "heizung.h"
#include "protokoll.h"

/* Welcher AHT20-Bus gehoert zu welcher Kammer - am 10.11. per Heiztest bestaetigt: Bus 1 (PB12/PB13)
 * = links, Bus 2 (PB14/PB15) = rechts. */
#define AHT_LINKS  0
#define AHT_RECHTS 1

#define VERSION 0x00010301u   /* 1.3.1 */

/* Tasten (Bit in st.tasten, 10.11. per Zaehltest bestimmt) */
#define T_LR      (1u << 0)   /* PA2 */
#define T_RUNTER  (1u << 1)   /* PA3 */
#define T_HOCH    (1u << 2)   /* PA4 */
#define T_EINST   (1u << 3)   /* PA5 */
#define FOTO_SCHRITTE 14      /* Modus 8: 0 alles an, 1..5 Byte-Code, 6..13 Bitebene */

void ausgaenge_sicher(void);

/* ---------- Status und Steuerung (fester RAM-Bereich) ---------- */
typedef struct {
    uint32_t magic;            /* 0x314C3458 = "X4L1" */
    uint32_t version;
    uint32_t ms;
    uint32_t takt_hz;
    uint32_t reset_grund;      /* RCC_CSR beim Start */
    uint32_t tasten;           /* Bit0..3 = PA2..PA5 */
    uint32_t tasten_zaehler[4];
    int32_t  aht_t[2];         /* 0,01 °C */
    int32_t  aht_rh[2];        /* 0,01 % */
    uint32_t aht_ok[2];
    uint32_t aht_fehler[2];
    uint32_t aht_status[2];    /* letztes Statusbyte, 0xFFFFFFFF = keine Antwort */
    uint32_t ntc_roh[2];       /* ADC 0..4095, Mittel aus 16 */
    uint32_t lcd_rahmen;
    uint32_t lcd_nack;
    uint32_t luefter;          /* aktiver Luefter-Test, Bit0 = PB0, Bit1 = PB1 */
    uint32_t luefter_rest_ms;
    uint8_t  lcd_bild[20];     /* was gerade am Display liegt */
    /* ab 1.1.0: Bedienung (Offset 124) */
    uint32_t ansicht;          /* 0 links, 1 rechts */
    uint32_t feld;             /* 0 keins, 1 Material, 2 Temperatur, 3 Zeit */
    uint32_t laeuft[2];
    uint32_t rest_s[2];
    uint32_t soll[2];
    uint32_t dauer_min[2];
    uint32_t material[2];
    /* ab 1.2.0: Heizung (Offset 172) */
    uint32_t fehler[2];
    uint32_t leistung[2];      /* % */
    int32_t  ntc_t[2];         /* 0,1 °C Heizflaeche, -32768 = ungueltig */
    uint32_t luefter_ist[2];
    uint32_t heizung_ist[2];
    /* ab 1.3.0: UART-Protokoll (Offset 212) */
    uint32_t uart_befehle;     /* ausgefuehrte Befehle */
    uint32_t uart_fehler;      /* verworfene/abgelehnte Zeilen */
} status_t;

typedef struct {
    uint32_t magic;            /* 0x52455453 = "STER" wird von der Firmware gesetzt */
    uint32_t modus;            /* 0 Bedienung, 9 Original-Ruhebild, 1 alles aus, 2 alles an,
                                  3 nur Byte n, 4 nur Bit n, 5 roh[] anzeigen,
                                  6 Bitebene n (in jedem Byte Bit 0x80>>n),
                                  7 Byte-Code n (alle Bytes i mit Bit n in i),
                                  8 Fotoserie Schritt n (siehe FOTO_SCHRITTE) */
    uint32_t n;
    uint32_t luefter;          /* Bit0 = PB0, Bit1 = PB1; wird nach 2 s auf 0 gesetzt */
    uint8_t  roh[20];
    uint32_t befehl;           /* Offset 0x24: 1 = alle Kammern stoppen */
} steuer_t;

__attribute__((section(".status"))) volatile status_t st;
__attribute__((section(".steuer"))) volatile steuer_t sg;

/* Beispielbild fuer Testmodus 9 (Ruhezustand: 04:00:00, 26 °C, 40 %, Soll 75 °C, ABS) */
static const uint8_t ORIGINAL_BILD[20] = {
    0xe0, 0xbd, 0xbb, 0xdf, 0xb6, 0xbf, 0xbf, 0xbf, 0xff, 0x6d,
    0xfb, 0xd6, 0xbf, 0xbb, 0x04, 0x05, 0x04, 0xef, 0xff, 0xbd,
};

/* ---------- Zeit ---------- */
static volatile uint32_t ms_zaehler;
static uint32_t takt_hz = 8000000;

void SysTick_Handler(void) { ms_zaehler++; }

static uint32_t jetzt(void) { return ms_zaehler; }

static void warte_us(uint32_t us)
{
    uint32_t start = DWT_CYCCNT, dauer = us * (takt_hz / 1000000u);
    while (DWT_CYCCNT - start < dauer) ;
}

static void warte_ms(uint32_t ms)
{
    uint32_t start = jetzt();
    while (jetzt() - start < ms) IWDG_KR = 0xAAAA;
}

/* ---------- Takt: 8-MHz-Quarz x 9 = 72 MHz (geht auf GD32F303 und STM32F103) ---------- */
static void takt_einrichten(void)
{
    RCC_CR |= 1u << 16;                                   /* HSE an */
    uint32_t i;
    for (i = 0; i < 2000000u && !(RCC_CR & (1u << 17)); i++) ;
    if (RCC_CR & (1u << 17)) {
        FLASH_ACR = 0x12;                                 /* Prefetch, 2 Wartezyklen */
        RCC_CFGR = (7u << 18) | (1u << 16) | (4u << 8) | (2u << 14);  /* PLL x9 aus HSE, APB1/2, ADC/6 */
        RCC_CR |= 1u << 24;
        while (!(RCC_CR & (1u << 25))) ;
        RCC_CFGR |= 2u;
        while (((RCC_CFGR >> 2) & 3u) != 2u) ;
        takt_hz = 72000000u;
    }
    /* sonst weiter mit internem 8-MHz-Oszillator */
    SYST_RVR = takt_hz / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 7;                                         /* Prozessortakt, Interrupt, an */
    DEMCR |= 1u << 24;                                    /* DWT fuer Mikrosekunden */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
}

/* ---------- GPIO ---------- */
static void pin_modus(uint32_t port, uint32_t pin, uint32_t modus)
{
    volatile uint32_t *cr = pin < 8 ? &GPIO_CRL(port) : &GPIO_CRH(port);
    uint32_t s = (pin & 7u) * 4u;
    *cr = (*cr & ~(0xFu << s)) | (modus << s);
}

static void pin_setzen(uint32_t port, uint32_t pin, int hoch)
{
    if (hoch) GPIO_BSRR(port) = 1u << pin; else GPIO_BRR(port) = 1u << pin;
}

static int pin_lesen(uint32_t port, uint32_t pin) { return (GPIO_IDR(port) >> pin) & 1u; }

static void pins_einrichten(void)
{
    RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3) | (1u << 9) | (1u << 14); /* AFIO, A, B, ADC0, USART0 */
    ausgaenge_sicher();
    pin_modus(GPIOA, 0, M_ANALOG);
    pin_modus(GPIOA, 1, M_ANALOG);
    for (uint32_t p = 2; p <= 5; p++) { pin_setzen(GPIOA, p, 0); pin_modus(GPIOA, p, M_PULL); }  /* Pulldown */
    pin_setzen(GPIOA, 6, 1);  pin_modus(GPIOA, 6, M_OUT_PP);
    pin_setzen(GPIOB, 10, 1); pin_modus(GPIOB, 10, M_OUT_PP);
    pin_setzen(GPIOB, 11, 1); pin_modus(GPIOB, 11, M_OUT_PP);
    for (uint32_t p = 12; p <= 15; p++) { pin_setzen(GPIOB, p, 1); pin_modus(GPIOB, p, M_PULL); } /* losgelassen, Pullup */
    AFIO_MAPR |= 1u << 2;                                 /* USART0 -> PB6/PB7 */
    pin_modus(GPIOB, 6, M_AF_PP);
    pin_setzen(GPIOB, 7, 1); pin_modus(GPIOB, 7, M_PULL);
}

/* ---------- Display: Software-I2C an PB10 (SCL) / PB11 (SDA) ---------- */
#define LCD_T 10   /* halbe Taktperiode in us -> ca. 50 kHz */

static void lcd_scl(int h) { pin_setzen(GPIOB, 10, h); }
static void lcd_sda(int h) { pin_setzen(GPIOB, 11, h); }

static void lcd_start(void)
{
    lcd_sda(1); lcd_scl(1); warte_us(LCD_T);
    lcd_sda(0); warte_us(LCD_T);
    lcd_scl(0); warte_us(LCD_T);
}

static void lcd_stop(void)
{
    lcd_sda(0); warte_us(LCD_T);
    lcd_scl(1); warte_us(LCD_T);
    lcd_sda(1); warte_us(LCD_T);
}

static int lcd_byte(uint8_t b)   /* 1 = Quittung erhalten */
{
    for (int i = 7; i >= 0; i--) {
        lcd_sda((b >> i) & 1);
        warte_us(LCD_T / 2);
        lcd_scl(1); warte_us(LCD_T);
        lcd_scl(0); warte_us(LCD_T / 2);
    }
    /* SDA zum Quittieren loslassen (Eingang mit Pullup), anders als das Original
     * das hier gegen den Treiber arbeitet */
    pin_setzen(GPIOB, 11, 1); pin_modus(GPIOB, 11, M_PULL);
    warte_us(LCD_T / 2);
    lcd_scl(1); warte_us(LCD_T / 2);
    int ack = !pin_lesen(GPIOB, 11);
    warte_us(LCD_T / 2);
    lcd_scl(0);
    pin_setzen(GPIOB, 11, 0); pin_modus(GPIOB, 11, M_OUT_PP);
    warte_us(LCD_T / 2);
    if (!ack) st.lcd_nack++;
    return ack;
}

static void lcd_befehl(uint8_t b)
{
    lcd_start();
    if (lcd_byte(0x7E)) lcd_byte(b);
    lcd_stop();
}

static void lcd_bild_senden(const uint8_t *bild)
{
    lcd_start();
    if (lcd_byte(0x7E) && lcd_byte(0x00)) {
        for (int i = 0; i < 20; i++) if (!lcd_byte(bild[i])) break;
    }
    lcd_stop();
    for (int i = 0; i < 20; i++) st.lcd_bild[i] = bild[i];
    st.lcd_rahmen++;
}

static void lcd_einrichten(void)
{
    warte_ms(5);
    lcd_befehl(0x9C);   /* Befehlsfolge wie im Original */
    lcd_befehl(0xC0);
    lcd_befehl(0x72);
}

/* ---------- AHT20: Software-I2C, Pseudo-Open-Drain mit internem Pullup ---------- */
#define AHT_T 5
static const uint8_t AHT_SCL[2] = { 12, 14 };
static const uint8_t AHT_SDA[2] = { 13, 15 };

static void od(uint32_t pin, int hoch)
{
    if (hoch) { pin_modus(GPIOB, pin, M_PULL); pin_setzen(GPIOB, pin, 1); }
    else      { pin_setzen(GPIOB, pin, 0); pin_modus(GPIOB, pin, M_OUT_OD); }
}

static void a_start(int k) { od(AHT_SDA[k], 1); od(AHT_SCL[k], 1); warte_us(AHT_T); od(AHT_SDA[k], 0); warte_us(AHT_T); od(AHT_SCL[k], 0); warte_us(AHT_T); }
static void a_stop(int k)  { od(AHT_SDA[k], 0); warte_us(AHT_T); od(AHT_SCL[k], 1); warte_us(AHT_T); od(AHT_SDA[k], 1); warte_us(AHT_T); }

static int a_schreiben(int k, uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        od(AHT_SDA[k], (b >> i) & 1); warte_us(AHT_T);
        od(AHT_SCL[k], 1); warte_us(AHT_T);
        od(AHT_SCL[k], 0);
    }
    od(AHT_SDA[k], 1); warte_us(AHT_T);
    od(AHT_SCL[k], 1); warte_us(AHT_T);
    int ack = !pin_lesen(GPIOB, AHT_SDA[k]);
    od(AHT_SCL[k], 0); warte_us(AHT_T);
    return ack;
}

static uint8_t a_lesen(int k, int quittieren)
{
    uint8_t b = 0;
    od(AHT_SDA[k], 1);
    for (int i = 0; i < 8; i++) {
        warte_us(AHT_T); od(AHT_SCL[k], 1); warte_us(AHT_T);
        b = (uint8_t)(b << 1) | (uint8_t)pin_lesen(GPIOB, AHT_SDA[k]);
        od(AHT_SCL[k], 0);
    }
    od(AHT_SDA[k], quittieren ? 0 : 1); warte_us(AHT_T);
    od(AHT_SCL[k], 1); warte_us(AHT_T);
    od(AHT_SCL[k], 0); od(AHT_SDA[k], 1); warte_us(AHT_T);
    return b;
}

static int aht_befehl(int k, uint8_t a, uint8_t b, uint8_t c)
{
    a_start(k);
    int ok = a_schreiben(k, 0x70) && a_schreiben(k, a) && a_schreiben(k, b) && a_schreiben(k, c);
    a_stop(k);
    return ok;
}

static int aht_lesen(int k, uint8_t *d, int n)
{
    a_start(k);
    if (!a_schreiben(k, 0x71)) { a_stop(k); return 0; }
    for (int i = 0; i < n; i++) d[i] = a_lesen(k, i < n - 1);
    a_stop(k);
    return 1;
}

static uint8_t crc8(const uint8_t *d, int n)
{
    uint8_t c = 0xFF;
    while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1); }
    return c;
}

/* Zustandsautomat je Sensor: 0 pruefen/kalibrieren, 1 Messung starten, 2 Ergebnis holen */
static uint8_t aht_zustand[2];
static uint32_t aht_zeit[2];

static void aht_schritt(int k)
{
    uint32_t t = jetzt();
    uint8_t d[7];
    switch (aht_zustand[k]) {
    case 0:
        if ((int32_t)(t - aht_zeit[k]) < 100) return;   /* vorzeichenbehaftet: aht_zeit kann in der Zukunft liegen */
        if (!aht_lesen(k, d, 1)) { st.aht_status[k] = 0xFFFFFFFFu; st.aht_fehler[k]++; aht_zeit[k] = t + 900; return; }
        st.aht_status[k] = d[0];
        if (!(d[0] & 0x08)) { aht_befehl(k, 0xBE, 0x08, 0x00); aht_zeit[k] = t; return; }
        aht_zustand[k] = 1;
        /* fall through */
    case 1:
        if ((int32_t)(t - aht_zeit[k]) < 0) return;
        if (!aht_befehl(k, 0xAC, 0x33, 0x00)) { st.aht_fehler[k]++; st.aht_status[k] = 0xFFFFFFFFu; aht_zustand[k] = 0; aht_zeit[k] = t + 900; return; }
        aht_zeit[k] = t; aht_zustand[k] = 2;
        return;
    case 2:
        if (t - aht_zeit[k] < 90) return;
        if (!aht_lesen(k, d, 7)) { st.aht_fehler[k]++; aht_zustand[k] = 0; aht_zeit[k] = t + 900; return; }
        st.aht_status[k] = d[0];
        if (d[0] & 0x80) { if (t - aht_zeit[k] > 300) { st.aht_fehler[k]++; aht_zustand[k] = 1; aht_zeit[k] = t + 700; } return; }
        if (crc8(d, 6) != d[6]) { st.aht_fehler[k]++; aht_zustand[k] = 1; aht_zeit[k] = t + 900; return; }
        {
            uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
            uint32_t tt = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
            st.aht_rh[k] = (int32_t)(((uint64_t)rh * 10000u) >> 20);
            st.aht_t[k]  = (int32_t)(((uint64_t)tt * 20000u) >> 20) - 5000;
            st.aht_ok[k]++;
        }
        aht_zustand[k] = 1; aht_zeit[k] = t + 900;   /* jede Sekunde neu */
        return;
    }
}

/* ---------- NTC ueber ADC ---------- */
static void adc_einrichten(void)
{
    ADC_CR2 = 1u;                       /* einschalten */
    warte_ms(2);
    ADC_CR2 |= 1u << 3; while (ADC_CR2 & (1u << 3)) ;   /* Kalibrierung zuruecksetzen */
    ADC_CR2 |= 1u << 2; while (ADC_CR2 & (1u << 2)) ;   /* kalibrieren */
    ADC_SMPR2 = 7u | (7u << 3);         /* Kanal 0 und 1: laengste Abtastzeit */
    ADC_SQR1 = 0;                       /* eine Wandlung */
    ADC_CR2 |= (7u << 17) | (1u << 20); /* Start per Software */
}

static uint32_t adc_wandeln(uint32_t kanal)
{
    ADC_SQR3 = kanal;
    ADC_SR = 0;
    ADC_CR2 |= 1u << 22;
    uint32_t t = jetzt();
    while (!(ADC_SR & 2u)) if (jetzt() - t > 5) return 0xFFFF;
    return ADC_DR & 0xFFFu;
}

/* ---------- UART-Ausgabe ---------- */
static void uart_einrichten(void)
{
    USART_BRR = takt_hz / 115200u;
    USART_CR1 = (1u << 13) | (1u << 5) | (1u << 3) | (1u << 2);   /* UE, RXNEIE, TE, RE */
    NVIC_ISER1 = 1u << (USART0_IRQ - 32);
}

/* ---------- Watchdog: ca. 2 s ---------- */
static void watchdog_starten(void)
{
    IWDG_KR = 0x5555; IWDG_PR = 4; IWDG_RLR = 1250; IWDG_KR = 0xAAAA; IWDG_KR = 0xCCCC;
}

/* ---------- Hauptprogramm ---------- */
int main(void)
{
    uint32_t reset_grund = RCC_CSR;
    RCC_CSR |= 1u << 24;                /* Reset-Flags loeschen */
    pins_einrichten();
    takt_einrichten();
    watchdog_starten();

    for (unsigned i = 0; i < sizeof(st); i++) ((volatile uint8_t *)&st)[i] = 0;
    st.magic = 0x314C3458u; st.version = VERSION; st.takt_hz = takt_hz; st.reset_grund = reset_grund;
    for (unsigned i = 0; i < sizeof(sg); i++) ((volatile uint8_t *)&sg)[i] = 0;
    sg.magic = 0x52455453u;

    uart_einrichten();
    adc_einrichten();
    lcd_einrichten();
    bedienung_start();
    protokoll_senden_version(VERSION);

    uint32_t t_lcd = 0, t_ntc = 0, t_uart = 0, t_tasten = 0, version_zaehler = 0;
    uint32_t alt_modus = 0xFFFFFFFFu, alt_n = 0xFFFFFFFFu, alt_luefter = 0, luefter_start = 0;
    uint32_t ntc_summe[2] = { 0, 0 }, ntc_anzahl = 0;
    uint32_t aht_ok_alt[2] = { 0, 0 }, aht_zeit_ok[2] = { 0, 0 }, luefter_seit[2] = { 0, 0 };
    uint8_t luefter_an[2] = { 0, 0 };
    uint8_t bild[20];

    for (;;) {
        IWDG_KR = 0xAAAA;
        uint32_t t = jetzt();
        st.ms = t;

        /* Tasten (entprellt ueber 20-ms-Raster) */
        if (t - t_tasten >= 20) {
            t_tasten = t;
            uint32_t neu = (GPIO_IDR(GPIOA) >> 2) & 0xFu;
            uint32_t gedrueckt = neu & ~st.tasten;
            for (int i = 0; i < 4; i++) if (gedrueckt & (1u << i)) st.tasten_zaehler[i]++;
            st.tasten = neu;
            if (sg.modus == 0u) {
                if ((gedrueckt & T_EINST) && heizung[ansicht].fehler) heizung_quittieren(ansicht);
                bedienung_tasten(neu, t);
            } else if (sg.modus == 8u) {          /* Fotoserie (nur per SWD gestartet) */
                if (gedrueckt & T_EINST) sg.modus = 0;
                if ((gedrueckt & T_HOCH) && sg.n + 1u < FOTO_SCHRITTE) sg.n = sg.n + 1u;
                if ((gedrueckt & T_RUNTER) && sg.n > 0u) sg.n = sg.n - 1u;
            }
        }
        bedienung_takt(t);
        st.ansicht = ansicht; st.feld = feld;
        for (int k = 0; k < 2; k++) {
            st.laeuft[k] = kammer[k].laeuft; st.rest_s[k] = kammer[k].rest_s; st.soll[k] = kammer[k].soll;
            st.dauer_min[k] = kammer[k].dauer_min; st.material[k] = kammer[k].material;
        }

        /* SWD-Befehl 1 = alle Kammern sofort stoppen */
        if (sg.befehl == 1u) { kammer[0].laeuft = kammer[1].laeuft = 0; sg.befehl = 0; }

        /* Alter der Kammermesswerte */
        for (int b = 0; b < 2; b++) if (st.aht_ok[b] != aht_ok_alt[b]) { aht_ok_alt[b] = st.aht_ok[b]; aht_zeit_ok[b] = t; }

        /* Luefter-Test an PB0/PB1 per SWD, hoechstens 2 s */
        uint32_t lw = sg.luefter & 3u;
        if (lw != alt_luefter) { alt_luefter = lw; luefter_start = t; }
        if (lw && t - luefter_start >= 2000u) { sg.luefter = 0; lw = 0; alt_luefter = 0; }
        st.luefter = lw;
        st.luefter_rest_ms = lw ? 2000u - (t - luefter_start) : 0;

        /* ---------- Heizung und Luefter je Kammer ----------
         * Kammer 0 = links:  Heizung PA10, Luefter PB0, NTC PA0, AHT-Bus AHT_LINKS
         * Kammer 1 = rechts: Heizung PA9,  Luefter PB1, NTC PA1, AHT-Bus AHT_RECHTS */
        static const uint8_t HEIZ_PIN[2] = { 10, 9 }, LUEFTER_PIN[2] = { 0, 1 }, AHT_BUS[2] = { AHT_LINKS, AHT_RECHTS };
        for (int k = 0; k < 2; k++) {
            if (kammer[k].laeuft && heizung[k].fehler) kammer[k].laeuft = 0;     /* mit Fehler kein Start */
            int b = AHT_BUS[k];
            uint32_t alter = st.aht_ok[b] ? t - aht_zeit_ok[b] : 0xFFFFFFFFu;
            uint8_t heizt = (uint8_t)((GPIO_ODR(GPIOA) >> HEIZ_PIN[k]) & 1u);
            heizung_schritt(k, t, &kammer[k], st.aht_t[b], alter, st.ntc_roh[k], heizt);

            /* Luefter zuerst */
            int luefter = heizung[k].luefter_soll || ((lw >> k) & 1u);
            pin_setzen(GPIOB, LUEFTER_PIN[k], luefter);
            int luefter_ist = (int)((GPIO_ODR(GPIOB) >> LUEFTER_PIN[k]) & 1u);
            /* Merker statt "t | 1u": das konnte 1 ms in der Zukunft liegen, die Differenz lief
             * ueber und gab die Heizung im ersten Takt frei (Fehler bis 1.2.1) */
            if (!luefter_ist) luefter_an[k] = 0;
            else if (!luefter_an[k]) { luefter_an[k] = 1; luefter_seit[k] = t; }

            /* Heizung nur mit laufendem Luefter (seit >= 3 s) - Grundregel des Projekts */
            int darf = heizung[k].heizen_soll && !heizung[k].fehler && kammer[k].laeuft
                       && luefter_ist && luefter_an[k] && t - luefter_seit[k] >= LUEFTER_VORLAUF_MS;
            pin_setzen(GPIOA, HEIZ_PIN[k], darf);

            st.fehler[k] = heizung[k].fehler; st.leistung[k] = heizung[k].leistung;
            st.ntc_t[k] = heizung[k].ntc_t; st.luefter_ist[k] = (uint32_t)luefter_ist; st.heizung_ist[k] = (uint32_t)darf;
        }

        /* Display: Bedienung alle 100 ms, Testmodi bei Aenderung bzw. alle 500 ms */
        if (sg.modus == 0u) {
            if (t - t_lcd >= 100) {
                messwerte_t mw;
                static const int bus[2] = { AHT_LINKS, AHT_RECHTS };
                for (int k = 0; k < 2; k++) {
                    mw.t[k] = st.aht_t[bus[k]]; mw.rh[k] = st.aht_rh[bus[k]];
                    mw.ok[k] = st.aht_ok[bus[k]] != 0 && st.aht_status[bus[k]] != 0xFFFFFFFFu;
                    mw.fehler[k] = heizung[k].fehler; mw.heizt[k] = heizung[k].leistung > 0;
                }
                bedienung_bild(bild, &mw, t);
                lcd_bild_senden(bild);
                t_lcd = t;
            }
            alt_modus = 0;
        } else if (sg.modus != alt_modus || sg.n != alt_n || sg.modus == 5u || t - t_lcd >= 500) {
            uint32_t m = sg.modus, n = sg.n;
            if (m == 8u) {                         /* Fotoserie auf Modus 2/7/6 abbilden */
                if (n == 0u) m = 2;
                else if (n <= 5u) { m = 7; n = n - 1u; }
                else { m = 6; n = n - 6u; }
            }
            for (int i = 0; i < 20; i++) {
                switch (m) {
                case 6: bild[i] = (uint8_t)(0x80u >> (n % 8u)); break;
                case 7: bild[i] = ((uint32_t)i >> (n % 5u)) & 1u ? 0xFF : 0x00; break;
                case 9: bild[i] = ORIGINAL_BILD[i]; break;
                case 1: bild[i] = 0x00; break;
                case 2: bild[i] = 0xFF; break;
                case 3: bild[i] = (n % 20u == (uint32_t)i) ? 0xFF : 0x00; break;
                case 4: bild[i] = (n % 160u) / 8u == (uint32_t)i ? (uint8_t)(0x80u >> (n % 8u)) : 0x00; break;
                default: bild[i] = sg.roh[i]; break;
                }
            }
            if (m != 5u || t - t_lcd >= 100) { lcd_bild_senden(bild); t_lcd = t; }
            alt_modus = sg.modus; alt_n = sg.n;
        }

        /* NTC alle 10 ms messen, alle 160 ms mitteln */
        if (t - t_ntc >= 10) {
            t_ntc = t;
            ntc_summe[0] += adc_wandeln(0);
            ntc_summe[1] += adc_wandeln(1);
            if (++ntc_anzahl == 16) {
                st.ntc_roh[0] = ntc_summe[0] / 16u; st.ntc_roh[1] = ntc_summe[1] / 16u;
                ntc_summe[0] = ntc_summe[1] = 0; ntc_anzahl = 0;
            }
        }

        aht_schritt(0);
        aht_schritt(1);

        /* Protokoll zum ESP32: Befehle abarbeiten, jede Sekunde Status, alle 10 s Version */
        protokoll_befehle(t);
        if (t - t_uart >= 1000) {
            t_uart = t;
            kammer_status_t ks[2];
            static const uint8_t BUS[2] = { AHT_LINKS, AHT_RECHTS };
            for (int k = 0; k < 2; k++) {
                int b = BUS[k];
                ks[k].laeuft = kammer[k].laeuft; ks[k].soll = kammer[k].soll; ks[k].material = kammer[k].material;
                ks[k].dauer_min = kammer[k].dauer_min; ks[k].rest_s = kammer[k].rest_s;
                ks[k].fehler = heizung[k].fehler; ks[k].leistung = heizung[k].leistung;
                ks[k].luefter = (uint8_t)st.luefter_ist[k];
                ks[k].sensor_ok = st.aht_ok[b] != 0 && st.aht_status[b] != 0xFFFFFFFFu;
                ks[k].t = st.aht_t[b]; ks[k].rh = st.aht_rh[b]; ks[k].ntc = heizung[k].ntc_t;
            }
            protokoll_senden_status(ks);
            if (++version_zaehler >= 10) { version_zaehler = 0; protokoll_senden_version(VERSION); }
            st.uart_befehle = protokoll_befehle_gesamt(); st.uart_fehler = protokoll_fehler_gesamt();
        }
    }
}
