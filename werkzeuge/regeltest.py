"""Regeltest der X4L-Firmware (ab 1.2.0): protokolliert alle 2 s als CSV und stoppt per SWD bei
  - Heizung an ohne Luefter (darf nie passieren)
  - Heizflaeche >= GRENZE_NTC (Firmware regelt ab 110 °C ab, aus bei 120 °C)
  - Kammerluft > Soll + 8 K
  - andere Kammer > 8 K waermer als zu Beginn
  - Testdauer erreicht

  python werkzeuge/regeltest.py [dauer_s=900] [grenze_ntc=125]
Wartet, bis am Geraet eine Kammer gestartet wird.
"""
import csv
import os
import sys
import time

HIER = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HIER)
import swd_sichern as swd  # noqa: E402
import x4l  # noqa: E402

DAUER_S = float(sys.argv[1]) if len(sys.argv) > 1 else 900
GRENZE_NTC = float(sys.argv[2]) if len(sys.argv) > 2 else 125.0


def lesen():
    w = x4l.lese_worte(x4l.STATUS, 53)
    s = lambda i: w[i] - (1 << 32) if w[i] & 0x80000000 else w[i]  # noqa: E731
    return {"laeuft": w[33:35], "soll": w[37:39], "fehler": w[43:45], "leistung": w[45:47],
            "ntc": [s(47) / 10, s(48) / 10], "luefter": w[49:51], "heizung": w[51:53],
            "aht": [s(10) / 100, s(11) / 100], "rh": [s(12) / 100, s(13) / 100]}


def stopp():
    swd.openocd([f"mww 0x{x4l.STEUER + 0x24:08x} 1"])


w = x4l.lese_worte(x4l.STATUS, 2)
if w[0] != 0x314C3458 or w[1] < 0x00010200:
    sys.exit("Firmware 1.2.0 oder neuer noetig.")
print(f"Warte auf Start einer Kammer (Test {DAUER_S / 60:.0f} min) ...", flush=True)
while not any(lesen()["laeuft"]):
    time.sleep(1)
d = lesen()
k = 0 if d["laeuft"][0] else 1
a = 1 - k
soll = d["soll"][k]
start, aht_a0 = time.time(), d["aht"][a]
datei = os.path.join(os.path.dirname(HIER), "protokolle", time.strftime(f"regeltest-{'LR'[k]}-%Y%m%d-%H%M.csv"))
os.makedirs(os.path.dirname(datei), exist_ok=True)
print(f"Kammer {'LR'[k]} laeuft, Soll {soll} °C. Protokoll: {datei}", flush=True)
grund, letzte_ausgabe, max_ntc, max_kammer = None, -30, 0, 0
with open(datei, "w", newline="", encoding="utf-8") as f:
    out = csv.writer(f, delimiter=";")
    out.writerow(["s", "kammer_C", "soll_C", "heizflaeche_C", "leistung_%", "heizung", "luefter", "rh_%", "andere_kammer_C", "fehler"])
    while grund is None:
        d = lesen()
        dt = time.time() - start
        out.writerow([f"{dt:.1f}", d["aht"][k], soll, d["ntc"][k], d["leistung"][k], d["heizung"][k], d["luefter"][k],
                      d["rh"][k], d["aht"][a], d["fehler"][k]])
        f.flush()
        max_ntc, max_kammer = max(max_ntc, d["ntc"][k]), max(max_kammer, d["aht"][k])
        if dt - letzte_ausgabe >= 30:
            letzte_ausgabe = dt
            print(f"{dt / 60:5.1f} min  Kammer {d['aht'][k]:5.1f} °C (Soll {soll})  Heizflaeche {d['ntc'][k]:5.1f} °C  "
                  f"Leistung {d['leistung'][k]:3d} %  Feuchte {d['rh'][k]:4.1f} %  max Heizfl. {max_ntc:.1f}", flush=True)
        for i in (0, 1):
            if d["heizung"][i] and not d["luefter"][i]:
                grund = f"Heizung {'LR'[i]} an ohne Luefter!"
            if d["ntc"][i] >= GRENZE_NTC:
                grund = f"Heizflaeche {'LR'[i]} {d['ntc'][i]:.1f} °C"
        if d["aht"][k] > soll + 8:
            grund = f"Kammer {d['aht'][k]:.1f} °C > Soll + 8 K"
        if d["aht"][a] - aht_a0 > 8:
            grund = "andere Kammer wird warm"
        if not d["laeuft"][k]:
            grund = f"Kammer hat selbst gestoppt, Fehler {d['fehler']}"
        if dt >= DAUER_S:
            grund = "Testdauer erreicht"
        if grund is None:
            time.sleep(2)
if "selbst" not in grund:
    stopp()
print(f"\n*** Ende: {grund}.  Max. Kammer {max_kammer:.1f} °C, max. Heizflaeche {max_ntc:.1f} °C", flush=True)
