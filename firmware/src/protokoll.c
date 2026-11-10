#include "protokoll.h"
#include "regs.h"
#include "bedienung.h"
#include "heizung.h"

/* ---------- Empfang per Interrupt in einen Ringpuffer ---------- */
static volatile uint8_t rx_puffer[256];
static volatile uint8_t rx_kopf, rx_ende;
static uint32_t befehle, fehler;

void USART0_IRQHandler(void)
{
    uint32_t sr = USART_SR;
    if (sr & ((1u << 5) | (1u << 3))) {            /* RXNE oder Ueberlauf: DR lesen loescht beides */
        uint8_t c = (uint8_t)USART_DR;
        uint8_t n = (uint8_t)(rx_kopf + 1u);
        if (n != rx_ende) { rx_puffer[rx_kopf] = c; rx_kopf = n; }
    }
}

/* ---------- Senden ---------- */
static void zeichen(char c) { while (!(USART_SR & (1u << 7))) ; USART_DR = (uint8_t)c; }

static char zeile[96];
static unsigned laenge;

static void anhaengen(const char *s) { while (*s && laenge < sizeof(zeile) - 4u) zeile[laenge++] = *s++; }

static void zahl(int32_t v)
{
    char b[12]; int i = 0; uint32_t u;
    if (v < 0) { anhaengen("-"); u = (uint32_t)(-v); } else u = (uint32_t)v;
    do { b[i++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    char r[12]; int j = 0;
    while (i) r[j++] = b[--i];
    r[j] = 0;
    anhaengen(r);
}

static const char HEX[] = "0123456789ABCDEF";

static void absenden(void)                      /* zeile[0] ist '$' */
{
    uint8_t x = 0;
    for (unsigned i = 1; i < laenge; i++) x ^= (uint8_t)zeile[i];
    zeile[laenge++] = '*';
    zeile[laenge++] = HEX[x >> 4];
    zeile[laenge++] = HEX[x & 15u];
    for (unsigned i = 0; i < laenge; i++) zeichen(zeile[i]);
    zeichen('\r'); zeichen('\n');
    laenge = 0;
}

void protokoll_senden_status(const kammer_status_t s[2])
{
    for (int k = 0; k < 2; k++) {
        const kammer_status_t *q = &s[k];
        laenge = 0;
        anhaengen("$K,"); zahl(k);
        anhaengen(","); zahl(q->laeuft);
        anhaengen(","); zahl(q->soll);
        anhaengen(","); zahl(q->dauer_min);
        anhaengen(","); zahl((int32_t)q->rest_s);
        anhaengen(","); zahl(q->material);
        anhaengen(","); zahl(q->t);
        anhaengen(","); zahl(q->rh);
        anhaengen(","); zahl(q->ntc);
        anhaengen(","); zahl(q->leistung);
        anhaengen(","); zahl(q->fehler);
        anhaengen(","); zahl(q->luefter);
        anhaengen(","); zahl(q->sensor_ok);
        absenden();
    }
}

void protokoll_senden_version(uint32_t v)
{
    laenge = 0;
    anhaengen("$V,"); zahl((int32_t)(v >> 16)); anhaengen("."); zahl((int32_t)((v >> 8) & 255u));
    anhaengen("."); zahl((int32_t)(v & 255u));
    absenden();
}

static void antwort(int ok, const char *befehl, const char *zusatz)
{
    laenge = 0;
    anhaengen(ok ? "$OK," : "$ERR,"); anhaengen(befehl); anhaengen(","); anhaengen(zusatz);
    absenden();
    if (!ok) fehler++;
}

/* ---------- Befehle ---------- */
static char rx_zeile[48];
static unsigned rx_laenge;
static uint8_t rx_verworfen;

static int hexwert(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int gleich(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0; }

static int lese_zahl(const char *s, int32_t *v)
{
    int32_t r = 0; int n = 0;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9' || ++n > 6) return 0;
        r = r * 10 + (*s++ - '0');
    }
    *v = r;
    return 1;
}

static void ausfuehren(char *z, uint32_t t)
{
    /* z = "!BEFEHL,k[,wert]*HH" ohne Zeilenende */
    char *stern = 0;
    for (char *p = z; *p; p++) if (*p == '*') stern = p;
    if (!stern || hexwert(stern[1]) < 0 || hexwert(stern[2]) < 0 || stern[3]) { fehler++; return; }
    uint8_t x = 0;
    for (char *p = z + 1; p < stern; p++) x ^= (uint8_t)*p;
    if (x != (uint8_t)(hexwert(stern[1]) * 16 + hexwert(stern[2]))) { fehler++; return; }
    *stern = 0;

    char *teil[4] = { z + 1, 0, 0, 0 };
    int n = 1;
    for (char *p = z + 1; *p && n < 4; p++) if (*p == ',') { *p = 0; teil[n++] = p + 1; }
    const char *befehl = teil[0];
    int32_t k, wert = 0;
    if (n < 2 || !lese_zahl(teil[1], &k) || k > 1) { antwort(0, befehl, "KAMMER"); return; }
    if (n >= 3 && !lese_zahl(teil[2], &wert)) { antwort(0, befehl, "WERT"); return; }
    befehle++;

    int r;
    if (gleich(befehl, "START"))       r = fern_start((int)k, t);
    else if (gleich(befehl, "STOP"))   r = fern_stop((int)k, t);
    else if (gleich(befehl, "SOLL"))   r = n >= 3 ? fern_soll((int)k, (int)wert, t) : 1;
    else if (gleich(befehl, "DAUER"))  r = n >= 3 ? fern_dauer((int)k, (int)wert, t) : 1;
    else if (gleich(befehl, "MAT"))    r = n >= 3 ? fern_material((int)k, (int)wert, t) : 1;
    else if (gleich(befehl, "QUIT"))   { heizung_quittieren((int)k); r = heizung[k].fehler ? 2 : 0; }
    else { antwort(0, befehl, "UNBEKANNT"); return; }

    static const char *GRUND[] = { "", "WERT", "FEHLER", "LAEUFT" };
    if (r == 0) { char kk[2] = { (char)('0' + k), 0 }; antwort(1, befehl, kk); }
    else antwort(0, befehl, GRUND[r < 4 ? r : 1]);
}

void protokoll_befehle(uint32_t t)
{
    while (rx_ende != rx_kopf) {
        char c = (char)rx_puffer[rx_ende];
        rx_ende = (uint8_t)(rx_ende + 1u);
        if (c == '\r') continue;
        if (c == '\n') {
            rx_zeile[rx_laenge] = 0;
            if (!rx_verworfen && rx_laenge && rx_zeile[0] == '!') ausfuehren(rx_zeile, t);
            rx_laenge = 0; rx_verworfen = 0;
            continue;
        }
        if (rx_laenge < sizeof(rx_zeile) - 1u) rx_zeile[rx_laenge++] = c;
        else rx_verworfen = 1;                     /* zu lang: ganze Zeile verwerfen */
    }
}

uint32_t protokoll_befehle_gesamt(void) { return befehle; }
uint32_t protokoll_fehler_gesamt(void) { return fehler; }
