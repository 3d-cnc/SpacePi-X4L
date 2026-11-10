#include "bedienung.h"
#include "anzeige.h"
#include "einstellungen.h"
#include "heizung.h"

#define T_LR     (1u << 0)
#define T_RUNTER (1u << 1)
#define T_HOCH   (1u << 2)
#define T_EINST  (1u << 3)

#define SOLL_MIN 35
#define SOLL_MAX 75          /* Herstellerangabe X4L */
#define DAUER_MIN 30         /* Minuten */
#define DAUER_MAX (48 * 60)
#define LANG_MS 1500
#define FELD_TIMEOUT_MS 8000

typedef struct { char name[7]; uint8_t soll; uint16_t dauer_min; } material_t;
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
#define ANZAHL_MATERIAL (sizeof(MATERIAL) / sizeof(MATERIAL[0]))

kammer_t kammer[2];
uint8_t ansicht, feld;

static uint32_t alt_tasten, druck_start[4], wiederholung, letzte_aktion, sekunde;
static uint8_t lang_erledigt, speichern_noetig;

static void material_setzen(kammer_t *k, uint8_t m)
{
    k->material = m;
    k->soll = MATERIAL[m].soll;
    k->dauer_min = MATERIAL[m].dauer_min;
}

static void speichern(void)
{
    einstellungen_t e;
    for (int i = 0; i < 2; i++) {
        e.material[i] = kammer[i].material;
        e.soll[i] = kammer[i].soll;
        e.dauer_min[i] = kammer[i].dauer_min;
    }
    einst_speichern(&e);
    speichern_noetig = 0;
}

void bedienung_start(void)
{
    einstellungen_t e;
    int ok = einst_laden(&e);
    for (int i = 0; i < 2; i++) {
        kammer_t *k = &kammer[i];
        if (ok && e.material[i] < ANZAHL_MATERIAL && e.soll[i] >= SOLL_MIN && e.soll[i] <= SOLL_MAX
            && e.dauer_min[i] >= DAUER_MIN && e.dauer_min[i] <= DAUER_MAX) {
            k->material = e.material[i]; k->soll = e.soll[i]; k->dauer_min = e.dauer_min[i];
        } else {
            material_setzen(k, 0);
        }
        k->laeuft = 0; k->rest_s = 0; k->gesamt_s = 0;
    }
    ansicht = 0; feld = 0;
}

static void starten_stoppen(kammer_t *k)
{
    if (k->laeuft) { k->laeuft = 0; k->rest_s = 0; return; }
    k->gesamt_s = (uint32_t)k->dauer_min * 60u;
    k->rest_s = k->gesamt_s;
    k->laeuft = 1;
}

static void wert_aendern(int richtung)
{
    kammer_t *k = &kammer[ansicht];
    switch (feld) {
    case 1:
        if (k->laeuft) return;
        material_setzen(k, (uint8_t)((k->material + ANZAHL_MATERIAL + (unsigned)richtung) % ANZAHL_MATERIAL));
        break;
    case 2: {
        int s = k->soll + richtung;
        if (s < SOLL_MIN) s = SOLL_MIN;
        if (s > SOLL_MAX) s = SOLL_MAX;
        k->soll = (uint8_t)s;
        break;
    }
    case 3: {
        if (k->laeuft) return;
        int d = k->dauer_min + richtung * 30;
        if (d < DAUER_MIN) d = DAUER_MIN;
        if (d > DAUER_MAX) d = DAUER_MAX;
        k->dauer_min = (uint16_t)d;
        break;
    }
    default:
        return;
    }
    speichern_noetig = 1;
}

void bedienung_tasten(uint32_t tasten, uint32_t t)
{
    uint32_t gedrueckt = tasten & ~alt_tasten, losgelassen = alt_tasten & ~tasten;
    for (int i = 0; i < 4; i++) if (gedrueckt & (1u << i)) druck_start[i] = t;
    if (gedrueckt) letzte_aktion = t;

    if (gedrueckt & T_LR) {
        ansicht ^= 1u; feld = 0;
        if (speichern_noetig) speichern();
    }

    /* Zahnrad: kurz = naechstes Feld, lang = Start/Stopp */
    if (gedrueckt & T_EINST) lang_erledigt = 0;
    if ((tasten & T_EINST) && !lang_erledigt && t - druck_start[3] >= LANG_MS) {
        lang_erledigt = 1; feld = 0;
        if (speichern_noetig) speichern();
        starten_stoppen(&kammer[ansicht]);
    }
    if ((losgelassen & T_EINST) && !lang_erledigt) {
        feld = (uint8_t)((feld + 1u) % 4u);
        if (kammer[ansicht].laeuft && (feld == 1 || feld == 3)) feld = feld == 1 ? 2 : 0;   /* waehrend des Laufs nur Temperatur */
        if (feld == 0 && speichern_noetig) speichern();
    }

    /* Pfeile mit Wiederholung */
    if (gedrueckt & T_HOCH) { wert_aendern(+1); wiederholung = t + 600; }
    if (gedrueckt & T_RUNTER) { wert_aendern(-1); wiederholung = t + 600; }
    if ((tasten & (T_HOCH | T_RUNTER)) && !(gedrueckt & (T_HOCH | T_RUNTER)) && (int32_t)(t - wiederholung) >= 0) {
        wert_aendern((tasten & T_HOCH) ? +1 : -1);
        wiederholung = t + 120;
        letzte_aktion = t;
    }
    alt_tasten = tasten;
}

/* ---------- Fernsteuerung ---------- */
static uint32_t fern_aenderung;

int material_anzahl(void) { return (int)ANZAHL_MATERIAL; }

int fern_start(int k, uint32_t t)
{
    (void)t;
    if (kammer[k].laeuft) return 0;
    if (heizung[k].fehler) return 2;
    starten_stoppen(&kammer[k]);
    return 0;
}

int fern_stop(int k, uint32_t t)
{
    (void)t;
    if (kammer[k].laeuft) starten_stoppen(&kammer[k]);
    return 0;
}

int fern_soll(int k, int grad, uint32_t t)
{
    if (grad < SOLL_MIN || grad > SOLL_MAX) return 1;
    kammer[k].soll = (uint8_t)grad;
    speichern_noetig = 1; fern_aenderung = t;
    return 0;
}

int fern_dauer(int k, int minuten, uint32_t t)
{
    if (minuten < DAUER_MIN || minuten > DAUER_MAX) return 1;
    if (kammer[k].laeuft) return 3;
    kammer[k].dauer_min = (uint16_t)minuten;
    speichern_noetig = 1; fern_aenderung = t;
    return 0;
}

int fern_material(int k, int index, uint32_t t)
{
    if (index < 0 || index >= (int)ANZAHL_MATERIAL) return 1;
    if (kammer[k].laeuft) return 3;
    material_setzen(&kammer[k], (uint8_t)index);
    speichern_noetig = 1; fern_aenderung = t;
    return 0;
}

void bedienung_takt(uint32_t t)
{
    if (feld && t - letzte_aktion >= FELD_TIMEOUT_MS) {
        feld = 0;
        if (speichern_noetig) speichern();
    }
    if (!feld && speichern_noetig && t - fern_aenderung >= 10000u) speichern();   /* Flash schonen */
    if (t - sekunde >= 1000) {
        sekunde += 1000;
        if (t - sekunde >= 1000) sekunde = t;           /* nach langer Pause nicht nachholen */
        for (int i = 0; i < 2; i++) {
            kammer_t *k = &kammer[i];
            if (k->laeuft && k->rest_s && --k->rest_s == 0) k->laeuft = 0;
        }
    }
}

static void zwei_stellen(uint8_t *b, int zehner, int einer, int wert, uint16_t hundert)
{
    if (wert >= 100) { bild_symbol(b, hundert, 1); wert -= 100; }
    if (wert >= 10) bild_ziffer(b, zehner, (char)('0' + wert / 10));
    bild_ziffer(b, einer, (char)('0' + wert % 10));
}

void bedienung_bild(uint8_t *b, const messwerte_t *m, uint32_t t)
{
    const kammer_t *k = &kammer[ansicht];
    const kammer_t *anders = &kammer[ansicht ^ 1u];
    int blink_aus = (t / 400u) & 1u;

    bild_leeren(b);
    /* feste Symbole */
    bild_symbol(b, S_ZAHNRAD, 1); bild_symbol(b, S_PFEIL_HOCH, 1);
    bild_symbol(b, S_PFEIL_RUNTER, 1); bild_symbol(b, S_LR_TASTE, 1);
    bild_symbol(b, S_TIME, 1); bild_symbol(b, S_MATERIAL, 1);
    bild_symbol(b, S_THERMOMETER, !(m->heizt[ansicht] && blink_aus)); bild_symbol(b, S_PV, 1);
    bild_symbol(b, S_TROPFEN, 1); bild_symbol(b, S_RH, 1); bild_symbol(b, S_PROZENT, 1);

    /* Kammer: angezeigte fest, die andere blinkt, wenn sie laeuft */
    int l_an = ansicht == 0 || (anders->laeuft && !blink_aus);
    int r_an = ansicht == 1 || (anders->laeuft && !blink_aus);
    bild_symbol(b, S_L, l_an);
    bild_symbol(b, S_R, r_an);

    /* Solltemperatur klein */
    if (!(feld == 2 && blink_aus)) {
        zwei_stellen(b, Z_KLEIN_10, Z_KLEIN_1, k->soll, S_KLEIN_HUNDERT);
        bild_symbol(b, S_KLEIN_GRAD, 1); bild_symbol(b, S_KLEIN_C_UNTEN, 1);
    }

    /* Istwerte */
    int kanal = ansicht;
    if (m->ok[kanal] && m->t[kanal] >= 0) {
        zwei_stellen(b, Z_GROSS_10, Z_GROSS_1, (int)((m->t[kanal] + 50) / 100), S_GROSS_HUNDERT);
    } else {
        bild_ziffer(b, Z_GROSS_10, '-'); bild_ziffer(b, Z_GROSS_1, '-');
    }
    bild_symbol(b, S_GROSS_GRAD, 1); bild_symbol(b, S_GROSS_C_UNTEN, 1);
    if (m->ok[kanal]) {
        int rh = (int)((m->rh[kanal] + 50) / 100);
        if (rh > 99) rh = 99;
        if (rh < 0) rh = 0;
        zwei_stellen(b, Z_FEUCHTE_10, Z_FEUCHTE_1, rh, S_PROZENT);
    } else {
        bild_ziffer(b, Z_FEUCHTE_10, '-'); bild_ziffer(b, Z_FEUCHTE_1, '-');
    }

    /* Zeit: laufend = Restzeit, sonst eingestellte Dauer */
    if (!(feld == 3 && blink_aus)) {
        uint32_t s = k->laeuft ? k->rest_s : (uint32_t)k->dauer_min * 60u;
        uint32_t h = s / 3600u, mi = (s / 60u) % 60u, se = s % 60u;
        if (h > 99) h = 99;
        char z[7] = { (char)('0' + h / 10), (char)('0' + h % 10), (char)('0' + mi / 10),
                      (char)('0' + mi % 10), (char)('0' + se / 10), (char)('0' + se % 10), 0 };
        bild_zeit(b, z);
        bild_symbol(b, S_DOPPELPUNKT1, 1);
        bild_symbol(b, S_DOPPELPUNKT2, !k->laeuft || (k->rest_s & 1u));
    }

    /* Material, bei Heizungsfehler blinkend "Err n" */
    if (m->fehler[ansicht]) {
        char err[7] = { 'E', 'r', 'r', ' ', (char)('0' + m->fehler[ansicht] % 10u), 0, 0 };
        if (!blink_aus) bild_text(b, Z_MATERIAL, 6, err);
    } else if (!(feld == 1 && blink_aus)) bild_text(b, Z_MATERIAL, 6, MATERIAL[k->material].name);

    /* Zustand */
    bild_symbol(b, S_ON, k->laeuft);
    bild_symbol(b, S_OFF, !k->laeuft);

    /* Ring: Stillstand = voll, beim Trocknen Restanteil im Uhrzeigersinn */
    static const uint16_t RING[7] = { S_RING_UNTEN_LINKS, S_RING_LINKS, S_RING_OBEN_LINKS, S_RING_OBEN,
                                      S_RING_OBEN_RECHTS, S_RING_RECHTS, S_RING_UNTEN_RECHTS };
    uint32_t n = 7;
    if (k->laeuft && k->gesamt_s) n = (k->rest_s * 7u + k->gesamt_s - 1u) / k->gesamt_s;
    for (uint32_t i = 0; i < n && i < 7; i++) bild_symbol(b, RING[i], 1);
}
