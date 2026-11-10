"""Werkzeug fuer die eigene X4L-Firmware ueber den ST-Link (SWD).

  python werkzeuge/x4l.py flashen          eigene Firmware schreiben (firmware/build/x4l.elf)
  python werkzeuge/x4l.py original [DATEI]  eigene Sicherung der Creality-Firmware zurueckschreiben
                                           (Standard: sicherung/x4l-original/gd32f303cbt6-original-128KB.bin)
  python werkzeuge/x4l.py status           Status der laufenden eigenen Firmware lesen
  python werkzeuge/x4l.py beobachten       Status jede Sekunde (Strg+C beendet)
  python werkzeuge/x4l.py modus M [N]      Display: 0 Original-Ruhebild, 1 aus, 2 alles an,
                                           3 nur Byte N, 4 nur Bit N, 5 Rohdaten
  python werkzeuge/x4l.py roh HEX          20 Byte Rohdaten anzeigen (setzt Modus 5)
  python werkzeuge/x4l.py stopp            alle Kammern sofort stoppen (ab 1.2.0)
  python werkzeuge/x4l.py luefter 0..3     Luefter-Test (Bit0 PB0, Bit1 PB1), schaltet nach 2 s ab (ab Firmware 1.0.2)
                                           ACHTUNG Firmware 1.0.0/1.0.1: dort schaltet dieser Befehl PA9/PA10 = HEIZUNGEN!

Flashen nur mit eigener Firmware ohne Heizungscode - die haelt PB0/PB1 immer auf Low.
Beim ersten Flashen (Creality-Firmware laeuft noch) Netzstecker ziehen und den ST-Link
mit VCC anschliessen. Mit laufender eigener Firmware darf das Netz anbleiben, dann aber
OHNE das VCC-Kabel des ST-Link.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swd_sichern as swd  # noqa: E402

PROJEKT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(PROJEKT, "firmware", "build", "x4l.elf")
ORIGINAL = os.path.join(PROJEKT, "sicherung", "x4l-original", "gd32f303cbt6-original-128KB.bin")
STATUS, STEUER = 0x20000000, 0x20000100


def pfad(p):
    return "{" + p.replace(os.sep, "/") + "}"


def ausgabe_pruefen(code, out, wort):
    if wort not in out:
        print(out[-3000:])
        sys.exit("Fehlgeschlagen.")


def lese_worte(adresse, anzahl):
    code, out = swd.openocd([f"mdw 0x{adresse:08x} {anzahl}"])
    worte = []
    for zeile in out.splitlines():
        m = re.match(r"0x([0-9a-f]{8}):((?:\s+[0-9a-f]{8})+)", zeile)
        if m:
            worte += [int(w, 16) for w in m.group(2).split()]
    if len(worte) < anzahl:
        print(out[-2000:])
        sys.exit("Lesen fehlgeschlagen - ST-Link angeschlossen?")
    return worte[:anzahl]


def status_text():
    w = lese_worte(STATUS, 31)
    s = lese_worte(STEUER, 9)
    if w[0] != 0x314C3458:
        return f"Keine eigene Firmware aktiv (Kennung 0x{w[0]:08x})."
    b = struct.pack("<31I", *w)
    (magic, version, ms, takt, reset, tasten, z0, z1, z2, z3, t0, t1, rh0, rh1, ok0, ok1, f0, f1,
     as0, as1, ntc0, ntc1, rahmen, nack, luefter, rest) = struct.unpack_from("<6I4I2i2i2I2I2I2I2I2I", b)
    bild = b[104:124]
    zeilen = [
        f"Firmware {version >> 16}.{version >> 8 & 255}.{version & 255}, Laufzeit {ms / 1000:.1f} s, Takt {takt / 1e6:.0f} MHz, Reset-Grund 0x{reset:08x}",
        f"Tasten jetzt {tasten:04b} (PA5..PA2), gedrueckt PA2={z0} PA3={z1} PA4={z2} PA5={z3}",
    ]
    for k, (t, rh, ok, f, a) in enumerate(((t0, rh0, ok0, f0, as0), (t1, rh1, ok1, f1, as1)), 1):
        stat = "keine Antwort" if a == 0xFFFFFFFF else f"Status 0x{a:02x}"
        wert = f"{t / 100:.2f} °C  {rh / 100:.2f} %" if ok else "--"
        zeilen.append(f"AHT20 Bus {k} (PB{10 + 2 * k}/PB{11 + 2 * k}): {wert}  ok={ok} Fehler={f} {stat}")
    zeilen.append(f"NTC PA0={ntc0}  PA1={ntc1}  (ADC 0..4095, {ntc0 * 3.3 / 4095:.3f} V / {ntc1 * 3.3 / 4095:.3f} V)")
    zeilen.append(f"Display: Rahmen={rahmen} NACK={nack}  Modus={s[1]} n={s[2]}  Bild={bild.hex(' ')}")
    zeilen.append(f"Luefter-Test: {luefter} (Rest {rest / 1000:.1f} s)")
    try:
        import lcd_bild
        zeilen.append("Display zeigt: " + lcd_bild.lesen(bild).replace("\n", "\n               "))
    except Exception as fehler:  # noqa: BLE001
        zeilen.append(f"Display: nicht lesbar ({fehler})")
    if version >= 0x00010100:
        u = lese_worte(STATUS + 124, 12)
        felder = ["-", "Material", "Temperatur", "Zeit"]
        zeilen.append(f"Bedienung: Ansicht {'LR'[u[0]]}, Feld {felder[u[1]] if u[1] < 4 else u[1]}")
        for k in range(2):
            zustand = f"laeuft, Rest {u[4 + k] // 3600}:{u[4 + k] // 60 % 60:02d}:{u[4 + k] % 60:02d}" if u[2 + k] else "aus"
            zeilen.append(f"  Kammer {'LR'[k]}: {zustand}, Soll {u[6 + k]} °C, Dauer {u[8 + k] // 60}:{u[8 + k] % 60:02d} h, Material {u[10 + k]}")
    if version >= 0x00010200:
        h = lese_worte(STATUS + 172, 10)
        ntc = [(v - (1 << 32) if v & 0x80000000 else v) / 10 for v in h[4:6]]
        for k in range(2):
            zeilen.append(f"  Heizung {'LR'[k]}: Fehler {h[k]}, Leistung {h[2 + k]} %, Heizflaeche {ntc[k]:.1f} °C, "
                          f"Luefter {'an' if h[6 + k] else 'aus'}, Heizung {'AN' if h[8 + k] else 'aus'}")
    return "\n".join(zeilen)


def schreiben(adresse, wert):
    code, out = swd.openocd([f"mww 0x{adresse:08x} 0x{wert:08x}"])
    if "rror" in out and "Info" not in out:
        print(out[-2000:])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    befehl = sys.argv[1]
    if befehl == "flashen":
        if not os.path.exists(ELF):
            sys.exit("Erst bauen: python firmware/bauen.py")
        code, out = swd.openocd([f"program {pfad(ELF)} verify reset"])
        ausgabe_pruefen(code, out, "Verified OK")
        print("Geschrieben und geprueft, Firmware startet.")
    elif befehl == "original":
        datei = sys.argv[2] if len(sys.argv) > 2 else ORIGINAL
        if not os.path.exists(datei):
            sys.exit(f"Keine Sicherung gefunden: {datei}\n(Sie entsteht mit 'python werkzeuge/swd_sichern.py sichern' "
                     "vor dem ersten Flashen. Die Creality-Firmware liegt aus rechtlichen Gruenden nicht im Repo.)")
        if os.path.getsize(datei) != 131072:
            sys.exit(f"{datei} ist nicht 128 KB gross - keine vollstaendige Sicherung.")
        code, out = swd.openocd([f"program {pfad(datei)} 0x08000000 verify reset"])
        ausgabe_pruefen(code, out, "Verified OK")
        print("Creality-Firmware zurueckgeschrieben und geprueft.")
    elif befehl == "status":
        print(status_text())
    elif befehl == "beobachten":
        try:
            while True:
                print(status_text())
                print("-" * 60)
                time.sleep(1)
        except KeyboardInterrupt:
            pass
    elif befehl == "modus":
        if len(sys.argv) > 3:
            schreiben(STEUER + 8, int(sys.argv[3], 0))
        schreiben(STEUER + 4, int(sys.argv[2], 0))
        print(status_text())
    elif befehl == "roh":
        daten = bytes.fromhex(sys.argv[2].replace(" ", ""))
        if len(daten) != 20:
            sys.exit("Genau 20 Byte angeben.")
        cmds = [f"mww 0x{STEUER + 16 + 4 * i:08x} 0x{struct.unpack_from('<I', daten, 4 * i)[0]:08x}" for i in range(5)]
        cmds.append(f"mww 0x{STEUER + 4:08x} 5")
        swd.openocd(cmds)
        print(status_text())
    elif befehl == "stopp":
        swd.openocd([f"mww 0x{STEUER + 0x24:08x} 1"])
        print(status_text())
    elif befehl == "luefter":
        magic, version = lese_worte(STATUS, 2)
        if magic != 0x314C3458 or version < 0x00010002:
            # Vor 1.0.2 schaltet dieses Steuerwort PA9/PA10 = die HEIZUNGEN.
            sys.exit(f"Abgelehnt: Firmware 0x{version:08x} ist aelter als 1.0.2 - erst flashen.")
        schreiben(STEUER + 12, int(sys.argv[2], 0) & 3)
        print(status_text())
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
