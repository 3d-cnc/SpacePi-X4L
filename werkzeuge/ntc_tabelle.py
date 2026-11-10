"""Erzeugt firmware/src/ntc_tabelle.h aus der Steinhart-Hart-Gleichung.

Die Heizungsfuehler des X4L sind 100-kOhm-NTCs an einem 4,7-kOhm-Spannungsteiler nach 3,3 V
(R = 4700 * ADC / (4096 - ADC)). Die drei Koeffizienten wurden an Messpunkte des verbauten
Fuehlers angepasst (0, 60, 130 °C); Abweichung ueber -15..150 °C unter 0,2 K.

  python werkzeuge/ntc_tabelle.py
"""
import math
import os

A, B, C = 7.010326e-04, 2.175973e-04, 9.620308e-08   # 1/T = A + B ln R + C (ln R)^3, T in K
T_MIN, PUNKTE = -15, 166                               # 1-°C-Schritte von -15 bis 150 °C


def widerstand(t_c):
    """Loest die Steinhart-Hart-Gleichung nach R auf."""
    y = 1.0 / (t_c + 273.15)
    x = (A - y) / C
    w = math.sqrt((B / (3 * C)) ** 3 + (x / 2) ** 2)
    return math.exp(math.copysign(abs(w - x / 2) ** (1 / 3), w - x / 2) - math.copysign(abs(w + x / 2) ** (1 / 3), w + x / 2))


def main():
    werte = [round(widerstand(T_MIN + i)) for i in range(PUNKTE)]
    assert all(werte[i] > werte[i + 1] for i in range(PUNKTE - 1)), "Kennlinie muss fallen"
    ziel = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "firmware", "src", "ntc_tabelle.h")
    zeilen = [
        "/* NTC-Kennlinie der Heizungsfuehler (100 kOhm, Teiler 4,7 kOhm nach 3,3 V).",
        " * Erzeugt von werkzeuge/ntc_tabelle.py aus der Steinhart-Hart-Gleichung",
        f" * (A={A:.6e}, B={B:.6e}, C={C:.6e}). Widerstand in Ohm je 1 Grad C,",
        f" * Index i gehoert zu {T_MIN} + i Grad C. Nicht von Hand aendern. */",
        "#ifndef NTC_TABELLE_H",
        "#define NTC_TABELLE_H",
        "#include <stdint.h>",
        f"#define NTC_PUNKTE {PUNKTE}",
        "static const uint32_t NTC_OHM[NTC_PUNKTE] = {",
    ]
    for i in range(0, PUNKTE, 8):
        zeilen.append("    " + ", ".join(f"{v}u" for v in werte[i:i + 8]) + ",")
    zeilen += ["};", "#endif", ""]
    with open(ziel, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(zeilen))
    print(f"{ziel}: {PUNKTE} Werte, 25 °C = {werte[25 - T_MIN]} Ohm, 100 °C = {werte[100 - T_MIN]} Ohm")


if __name__ == "__main__":
    main()
