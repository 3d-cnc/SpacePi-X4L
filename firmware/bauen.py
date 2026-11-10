"""Baut die X4L-Firmware mit dem ARM-GCC (arm-none-eabi).

    python firmware/bauen.py        -> firmware/build/x4l.elf, x4l.bin, x4l.map

Gesucht wird der Compiler in dieser Reihenfolge: Umgebungsvariable ARM_GCC_BIN (Ordner),
PlatformIO-Paket toolchain-gccarmnoneeabi, PATH.
"""
import glob
import os
import shutil
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
AUS = os.path.join(HIER, "build")


def werkzeug(name):
    kandidaten = []
    if os.environ.get("ARM_GCC_BIN"):
        kandidaten.append(os.environ["ARM_GCC_BIN"])
    kandidaten.append(os.path.join(os.path.expanduser("~"), ".platformio", "packages", "toolchain-gccarmnoneeabi", "bin"))
    for ordner in kandidaten:
        for endung in (".exe", ""):
            p = os.path.join(ordner, name + endung)
            if os.path.isfile(p):
                return p
    p = shutil.which(name)
    if p:
        return p
    sys.exit(f"{name} nicht gefunden: ARM-GCC installieren "
             "(z. B. 'pio pkg install -g -t platformio/toolchain-gccarmnoneeabi') oder ARM_GCC_BIN setzen.")


GCC, OBJCOPY, SIZE = werkzeug("arm-none-eabi-gcc"), werkzeug("arm-none-eabi-objcopy"), werkzeug("arm-none-eabi-size")


def lauf(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout.strip():
        print(r.stdout.strip())
    if r.returncode:
        print(r.stderr)
        sys.exit("Bauen fehlgeschlagen.")
    if r.stderr.strip():
        print(r.stderr.strip())


os.makedirs(AUS, exist_ok=True)
elf = os.path.join(AUS, "x4l.elf")
lauf([GCC, "-mcpu=cortex-m3", "-mthumb", "-Os", "-g", "-std=c11",
      "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-nostdlib",
      "-ffunction-sections", "-fdata-sections",
      "-T", os.path.join(HIER, "link.ld"),
      "-Wl,--gc-sections", "-Wl,-Map=" + os.path.join(AUS, "x4l.map"),
      "-o", elf] + sorted(glob.glob(os.path.join(HIER, "src", "*.c"))) + ["-lgcc"])
lauf([OBJCOPY, "-O", "binary", elf, os.path.join(AUS, "x4l.bin")])
lauf([SIZE, elf])
print("OK:", elf)
