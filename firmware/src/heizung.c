#include "heizung.h"
#include "ntc_tabelle.h"

#define UNGUELTIG (-32768)

heizung_t heizung[2];

int16_t ntc_temperatur(uint32_t adc)
{
    if (adc < 100u || adc > 4050u) return UNGUELTIG;      /* kurzgeschlossen / offen */
    uint32_t r = 4700u * adc / (4096u - adc);
    unsigned i = 0;
    while (i < NTC_PUNKTE && r < NTC_OHM[i]) i++;
    if (i == 0 || i == NTC_PUNKTE) return UNGUELTIG;      /* ausserhalb der Kennlinie */
    uint32_t oben = NTC_OHM[i - 1], unten = NTC_OHM[i];
    return (int16_t)((int32_t)((oben - r) * 10u / (oben - unten)) + (int32_t)i * 10 - 160);
}

static void fehler_setzen(heizung_t *h, kammer_t *kam, uint8_t code, uint32_t t)
{
    if (h->fehler != 5) h->fehler = code;  /* der schwerste Fehler bleibt stehen */
    if (kam->laeuft) { kam->laeuft = 0; kam->rest_s = 0; }
    h->nachlauf = 1; h->nachlauf_start = t;
}

void heizung_quittieren(int k)
{
    if (heizung[k].fehler && heizung[k].fehler != 5) heizung[k].fehler = 0;
}

void heizung_schritt(int k, uint32_t t, kammer_t *kam, int32_t aht_t, uint32_t aht_alter_ms,
                     uint32_t ntc_adc, uint8_t heizt_wirklich)
{
    heizung_t *h = &heizung[k];
    h->ntc_t = ntc_temperatur(ntc_adc);

    /* ---- Schutz, unabhaengig vom Betriebszustand ---- */
    if (h->ntc_t == UNGUELTIG) {
        if (kam->laeuft || heizt_wirklich) fehler_setzen(h, kam, 2, t);
    } else {
        if (h->ntc_t >= NTC_FEHLER) fehler_setzen(h, kam, 3, t);
        /* Fehler 5: Heizung ist aus, die Nachwaerme (bis 90 s) ist vorbei, und die Heizflaeche
         * wird trotzdem ueber 60 °C und mehr als 10 K waermer als ihr Tiefstwert danach. */
        if (heizt_wirklich || !h->aus_aktiv) { h->aus_seit = t; h->aus_aktiv = 1; h->aus_basis = 0; }
        else if (t - h->aus_seit >= 90000u) {
            if (!h->aus_basis || h->ntc_t < h->aus_ntc_min) { h->aus_ntc_min = h->ntc_t; h->aus_basis = 1; }
            if (h->ntc_t > 600 && h->ntc_t - h->aus_ntc_min > 100) fehler_setzen(h, kam, 5, t);
        }
    }
    if (aht_alter_ms < 10000u && aht_t >= KAMMER_FEHLER) fehler_setzen(h, kam, 4, t);
    if (kam->laeuft && aht_alter_ms >= 5000u) fehler_setzen(h, kam, 1, t);

    /* ---- Regelung ---- */
    uint8_t leistung = 0;
    if (kam->laeuft && !h->fehler) {
        int32_t e = (int32_t)kam->soll * 100 - aht_t;      /* 0,01 K */
        if (e > 500) {
            leistung = 100;                                /* weit unter Soll: voll */
            h->im_pi = 0;
        } else {
            /* 1.2.1, abgestimmt nach dem Regeltest vom 10.11. (45 °C: Dauerleistung 13-15 %) */
            if (!h->im_pi) {                               /* Eintritt: I mit geschaetzter Dauerleistung vorbelegen */
                int32_t v = ((int32_t)kam->soll - 25) * 70;   /* 0,7 % je K ueber 25 °C, in 0,01 % */
                h->i_anteil = v < 0 ? 0 : v;
                h->i_zeit = t;
                h->im_pi = 1;
            }
            if (t - h->i_zeit >= 1000u) {                  /* einmal pro Sekunde: 0,03 % je K */
                h->i_zeit = t;
                if (e > -150) h->i_anteil += e * 3 / 100;
            }
            if (h->i_anteil < 0) h->i_anteil = 0;
            if (h->i_anteil > 5000) h->i_anteil = 5000;    /* hoechstens 50 % aus dem I-Anteil */
            int32_t p = e * 15 / 100;                      /* 15 % je K, auch bremsend ueber Soll */
            int32_t l = p + h->i_anteil / 100;
            if (e < -150) l = 0;                           /* mehr als 1,5 K ueber Soll: aus */
            if (l > 100) l = 100;
            if (l < 0) l = 0;
            leistung = (uint8_t)l;
        }
        /* Grenze der Heizflaeche: ab 110 °C abregeln, ab 120 °C aus */
        if (h->ntc_t >= NTC_REGEL_GRENZE) leistung = 0;
        else if (h->ntc_t > NTC_REGEL_GRENZE - 100) {
            uint32_t grenze = (uint32_t)(NTC_REGEL_GRENZE - h->ntc_t);   /* 0..100 */
            if (leistung > grenze) leistung = (uint8_t)grenze;
        }
        /* Fehler 6: 3 min mit mehr als 50 %, Heizflaeche nicht mindestens 3 K waermer */
        if (leistung > 50) {
            if (!h->pruef_aktiv) { h->pruef_aktiv = 1; h->pruef_start = t; h->pruef_ntc = h->ntc_t; }
            else if (t - h->pruef_start >= 180000u) {
                if (h->ntc_t - h->pruef_ntc < 30) fehler_setzen(h, kam, 6, t);
                h->pruef_aktiv = 0;
            }
        } else h->pruef_aktiv = 0;
    } else {
        h->i_anteil = 0; h->pruef_aktiv = 0; h->im_pi = 0;
    }
    h->leistung = leistung;
    h->heizen_soll = leistung && (t % FENSTER_MS) < (uint32_t)leistung * (FENSTER_MS / 100u);

    /* ---- Luefter: beim Trocknen immer, danach Nachlauf ---- */
    if (kam->laeuft) { h->nachlauf = 0; h->luefter_soll = 1; return; }
    if (!h->nachlauf && (h->ntc_t > NACHLAUF_BIS_NTC || heizt_wirklich)) { h->nachlauf = 1; h->nachlauf_start = t; }
    if (h->nachlauf) {
        uint32_t d = t - h->nachlauf_start;
        int kalt = h->ntc_t != UNGUELTIG && h->ntc_t <= NACHLAUF_BIS_NTC;
        if (h->fehler == 5 || d < NACHLAUF_MIN_MS || (!kalt && d < NACHLAUF_MAX_MS)) h->luefter_soll = 1;
        else { h->nachlauf = 0; h->luefter_soll = 0; }
    } else h->luefter_soll = 0;
}
