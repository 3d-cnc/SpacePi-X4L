"""Ueberwacht einen Heiztest der X4L-Firmware (ab 1.2.0) und stoppt per SWD sofort, wenn:
  - die Heizung einer Kammer an ist, ohne dass ihr Luefter laeuft (darf nie passieren),
  - die Heizflaeche (NTC) einer Kammer ueber GRENZE_NTC steigt,
  - beim Heizen der einen Kammer der NTC der ANDEREN Kammer um mehr als 3 K steigt
    (dann waere die Zuordnung Heizung <-> Kammer falsch),
  - die Testdauer DAUER_S erreicht ist.

  python werkzeuge/heiztest.py [dauer_s] [grenze_ntc]     (Standard 40 s, 70 °C)
Der Test startet erst, wenn am Geraet eine Kammer gestartet wurde (Zahnrad 1,5 s).
"""
import sys
import time

sys.path.insert(0, __file__.replace("\\", "/").rsplit("/", 1)[0])
import swd_sichern as swd  # noqa: E402
import x4l  # noqa: E402

DAUER_S = float(sys.argv[1]) if len(sys.argv) > 1 else 40
GRENZE_NTC = float(sys.argv[2]) if len(sys.argv) > 2 else 70.0


def lesen():
    w = x4l.lese_worte(x4l.STATUS, 53)
    s = lambda i: w[i] - (1 << 32) if w[i] & 0x80000000 else w[i]  # noqa: E731
    return {
        "laeuft": w[33:35], "fehler": w[43:45], "leistung": w[45:47],
        "ntc": [s(47) / 10, s(48) / 10], "luefter": w[49:51], "heizung": w[51:53],
        "aht": [s(10) / 100, s(11) / 100],
    }


def stopp(grund):
    swd.openocd([f"mww 0x{x4l.STEUER + 0x24:08x} 1"])
    print(f"\n*** GESTOPPT: {grund}", flush=True)


w = x4l.lese_worte(x4l.STATUS, 2)
if w[0] != 0x314C3458 or w[1] < 0x00010200:
    sys.exit("Firmware 1.2.0 oder neuer noetig.")
print(f"Warte auf Start einer Kammer (Test {DAUER_S:.0f} s, Grenze Heizflaeche {GRENZE_NTC:.0f} °C) ...", flush=True)
while not any(lesen()["laeuft"]):
    time.sleep(1)
d = lesen()
k = 0 if d["laeuft"][0] else 1
andere = 1 - k
start, ntc0 = time.time(), d["ntc"][:]
print(f"Kammer {'LR'[k]} laeuft. NTC vorher L {ntc0[0]:.1f} / R {ntc0[1]:.1f} °C", flush=True)
grund = None
while grund is None:
    d = lesen()
    dt = time.time() - start
    print(f"{dt:5.1f} s  Luefter L/R {d['luefter']}  Heizung L/R {d['heizung']}  Leistung {d['leistung']}  "
          f"NTC L {d['ntc'][0]:6.1f} R {d['ntc'][1]:6.1f}  Kammer L {d['aht'][0]:.1f} R {d['aht'][1]:.1f}  Fehler {d['fehler']}",
          flush=True)
    for i in (0, 1):
        if d["heizung"][i] and not d["luefter"][i]:
            grund = f"Heizung {'LR'[i]} an ohne Luefter!"
        if d["ntc"][i] >= GRENZE_NTC:
            grund = f"Heizflaeche {'LR'[i]} {d['ntc'][i]:.1f} °C >= {GRENZE_NTC:.0f} °C"
    if d["ntc"][andere] - ntc0[andere] > 3:
        grund = f"falsche Kammer wird warm ({'LR'[andere]})"
    if not d["laeuft"][k]:
        grund = f"Kammer hat selbst gestoppt, Fehler {d['fehler']}"
    if dt >= DAUER_S:
        grund = "Testdauer erreicht"
    if grund is None:
        time.sleep(1)
if "selbst" not in grund:
    stopp(grund)
else:
    print("\n*** " + grund, flush=True)
time.sleep(2)
d = lesen()
print(f"Danach: Heizung L/R {d['heizung']}  Luefter L/R {d['luefter']} (Nachlauf)  NTC L {d['ntc'][0]:.1f} R {d['ntc'][1]:.1f}", flush=True)
