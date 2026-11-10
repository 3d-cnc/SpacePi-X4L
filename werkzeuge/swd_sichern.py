"""Original-Firmware des GD32F303CBT6 (SpacePi X4L, Platine Dryer_Pro_V14) per ST-Link sichern.

NUR OHNE NETZSTECKER! Der ST-Link versorgt die Platine ueber VCC mit 3,3 V.
Grund: Haelt der Debugger den Chip an, bleiben seine Ausgaenge stehen - eine
gerade eingeschaltete Heizung bliebe eingeschaltet.

Aufruf:
  python werkzeuge/swd_sichern.py info      nur lesen: Chip-ID, Flash-Groesse, Leseschutz
  python werkzeuge/swd_sichern.py sichern   Flash zweimal lesen, vergleichen, speichern

Es wird nichts geschrieben oder geloescht.
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
ZIEL = os.path.join(os.path.dirname(HIER), "sicherung", "x4l-original")


def finde_openocd():
    """OpenOCD aus Umgebungsvariable OPENOCD, aus PlatformIO (tool-openocd) oder aus dem PATH."""
    if os.environ.get("OPENOCD"):
        return os.environ["OPENOCD"], None
    pio = glob.glob(os.path.join(os.path.expanduser("~"), ".platformio", "packages", "tool-openocd*", "bin", "openocd*"))
    if pio:
        return pio[0], os.path.join(os.path.dirname(os.path.dirname(pio[0])), "openocd", "scripts")
    gefunden = shutil.which("openocd")
    if gefunden:
        return gefunden, None
    sys.exit("OpenOCD nicht gefunden: installieren (z. B. 'pio pkg install -g -t platformio/tool-openocd') "
             "oder Pfad in der Umgebungsvariable OPENOCD angeben.")


OPENOCD, SCRIPTS = finde_openocd()

# Ohne "halt": nur lesende Speicherzugriffe ueber den AHB-AP.
# stlink-hla laeuft auch mit alten ST-Link-V2-Klonen (Firmware V2J23 und aelter).
GRUND = (["-s", SCRIPTS] if SCRIPTS else []) + ["-f", "interface/stlink-hla.cfg", "-c", "transport select hla_swd",
         "-c", "set CPUTAPID 0", "-c", "set FLASH_SIZE 0x40000", "-f", "target/stm32f1x.cfg",
         "-c", "adapter speed 1000"]
UNTER_RESET = ["-c", "reset_config srst_only srst_nogate connect_assert_srst"]


def openocd(befehle, unter_reset=False):
    args = [OPENOCD] + GRUND + (UNTER_RESET if unter_reset else []) + ["-c", "init"]
    for b in befehle:
        args += ["-c", b]
    args += ["-c", "shutdown"]
    r = subprocess.run(args, capture_output=True, text=True, timeout=180)
    return r.returncode, r.stdout + r.stderr


def wort(text, adresse):
    m = re.search(r"0x%08x:\s+([0-9a-f]{8})" % adresse, text)
    return int(m.group(1), 16) if m else None


def info():
    for unter_reset in (False, True):
        code, out = openocd(["mdw 0xE0042000", "mdw 0x1FFFF7E0", "mdw 0x4002201C", "mdw 0x1FFFF800 4"], unter_reset)
        idcode = wort(out, 0xE0042000)
        if idcode is not None:
            break
        print("Verbindung ohne Reset fehlgeschlagen, versuche unter Reset ..." if not unter_reset else "")
    if idcode is None:
        print(out[-2500:])
        sys.exit("Keine Verbindung zum Chip. Verkabelung, VCC und Netzstecker (muss gezogen sein) pruefen.")
    flash_kb = (wort(out, 0x1FFFF7E0) or 0) & 0xFFFF
    obstat = wort(out, 0x4002201C) or 0
    geschuetzt = bool(obstat & 0x2)
    print(f"DBGMCU IDCODE : 0x{idcode:08x}")
    print(f"Flash         : {flash_kb} KB")
    print(f"OBSTAT        : 0x{obstat:08x}")
    m = re.search(r"0x1ffff800:\s+((?:[0-9a-f]{8} ?)+)", out)
    print("Option Bytes  :", m.group(1).strip() if m else "?")
    print("Leseschutz    :", "AKTIV - Flash nicht auslesbar" if geschuetzt else "aus - Sicherung moeglich")
    return flash_kb, geschuetzt, unter_reset


def sichern():
    flash_kb, geschuetzt, unter_reset = info()
    if geschuetzt:
        sys.exit("Leseschutz aktiv: keine Sicherung moeglich. Es wurde nichts veraendert.")
    os.makedirs(ZIEL, exist_ok=True)
    groesse = flash_kb * 1024
    hashes = []
    for n in (1, 2):
        datei = os.path.join(ZIEL, f"lesen{n}.bin")
        code, out = openocd([f"dump_image {{{datei.replace(os.sep, '/')}}} 0x08000000 {groesse}",
                             f"dump_image {{{os.path.join(ZIEL, 'optionbytes.bin').replace(os.sep, '/')}}} 0x1FFFF800 16"],
                            unter_reset)
        if not os.path.exists(datei) or os.path.getsize(datei) != groesse:
            print(out[-2500:])
            sys.exit(f"Lesen {n} fehlgeschlagen.")
        hashes.append(hashlib.sha256(open(datei, "rb").read()).hexdigest())
        print(f"Lesen {n}: {groesse} Bytes, sha256 {hashes[-1][:16]}...")
    if hashes[0] != hashes[1]:
        sys.exit("Die beiden Lesevorgaenge unterscheiden sich - Verbindung unsauber, bitte wiederholen.")
    endgueltig = os.path.join(ZIEL, f"gd32f303cbt6-original-{flash_kb}KB.bin")
    os.replace(os.path.join(ZIEL, "lesen1.bin"), endgueltig)
    os.remove(os.path.join(ZIEL, "lesen2.bin"))
    leer = open(endgueltig, "rb").read().count(b"\xff") * 100 // groesse
    print(f"Gesichert: {endgueltig} ({leer} % leer/0xFF), sha256 {hashes[0]}")


if __name__ == "__main__":
    {"info": info, "sichern": sichern}.get(sys.argv[1] if len(sys.argv) > 1 else "info", info)()
