"""Builds the X4L firmware with the ARM GCC (arm-none-eabi).

    python firmware/build.py        -> firmware/build/x4l.elf, x4l.bin, x4l.map

The compiler is searched in this order: environment variable ARM_GCC_BIN (folder),
PlatformIO package toolchain-gccarmnoneeabi, PATH.
"""
import glob
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "build")


def tool(name):
    candidates = []
    if os.environ.get("ARM_GCC_BIN"):
        candidates.append(os.environ["ARM_GCC_BIN"])
    candidates.append(os.path.join(os.path.expanduser("~"), ".platformio", "packages", "toolchain-gccarmnoneeabi", "bin"))
    for folder in candidates:
        for ext in (".exe", ""):
            p = os.path.join(folder, name + ext)
            if os.path.isfile(p):
                return p
    p = shutil.which(name)
    if p:
        return p
    sys.exit(f"{name} not found: install the ARM GCC "
             "(e.g. 'pio pkg install -g -t platformio/toolchain-gccarmnoneeabi') or set ARM_GCC_BIN.")


GCC, OBJCOPY, SIZE = tool("arm-none-eabi-gcc"), tool("arm-none-eabi-objcopy"), tool("arm-none-eabi-size")


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.stdout.strip():
        print(r.stdout.strip())
    if r.returncode:
        print(r.stderr)
        sys.exit("Build failed.")
    if r.stderr.strip():
        print(r.stderr.strip())


os.makedirs(OUT, exist_ok=True)
elf = os.path.join(OUT, "x4l.elf")
run([GCC, "-mcpu=cortex-m3", "-mthumb", "-Os", "-g", "-std=c11",
     "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-nostdlib",
     "-ffunction-sections", "-fdata-sections",
     "-T", os.path.join(HERE, "link.ld"),
     "-Wl,--gc-sections", "-Wl,-Map=" + os.path.join(OUT, "x4l.map"),
     "-o", elf] + sorted(glob.glob(os.path.join(HERE, "src", "*.c"))) + ["-lgcc"])
run([OBJCOPY, "-O", "binary", elf, os.path.join(OUT, "x4l.bin")])
run([SIZE, elf])
print("OK:", elf)
