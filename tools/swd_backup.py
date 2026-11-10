"""Back up the original firmware of the GD32F303CBT6 (SpacePi X4L, board Dryer_Pro_V14) with a ST-Link.

ONLY WITH THE DRYER UNPLUGGED! The ST-Link supplies the board with 3.3 V over VCC.
Reason: when the debugger halts the chip, its outputs freeze - a heater that was
just switched on would stay on.

Usage:
  python tools/swd_backup.py info      read only: chip ID, flash size, read protection
  python tools/swd_backup.py backup    read the flash twice, compare, save

Nothing is written or erased.
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TARGET = os.path.join(os.path.dirname(HERE), "backup", "x4l-original")


def find_openocd():
    """OpenOCD from the environment variable OPENOCD, from PlatformIO (tool-openocd) or from the PATH."""
    if os.environ.get("OPENOCD"):
        return os.environ["OPENOCD"], None
    pio = glob.glob(os.path.join(os.path.expanduser("~"), ".platformio", "packages", "tool-openocd*", "bin", "openocd*"))
    if pio:
        return pio[0], os.path.join(os.path.dirname(os.path.dirname(pio[0])), "openocd", "scripts")
    found = shutil.which("openocd")
    if found:
        return found, None
    sys.exit("OpenOCD not found: install it (e.g. 'pio pkg install -g -t platformio/tool-openocd') "
             "or set its path in the environment variable OPENOCD.")


OPENOCD, SCRIPTS = find_openocd()

# Without "halt": only reading memory accesses over the AHB-AP.
# stlink-hla also works with old ST-Link V2 clones (firmware V2J23 and older).
BASE = (["-s", SCRIPTS] if SCRIPTS else []) + ["-f", "interface/stlink-hla.cfg", "-c", "transport select hla_swd",
        "-c", "set CPUTAPID 0", "-c", "set FLASH_SIZE 0x40000", "-f", "target/stm32f1x.cfg",
        "-c", "adapter speed 1000"]
UNDER_RESET = ["-c", "reset_config srst_only srst_nogate connect_assert_srst"]


def openocd(commands, under_reset=False):
    args = [OPENOCD] + BASE + (UNDER_RESET if under_reset else []) + ["-c", "init"]
    for c in commands:
        args += ["-c", c]
    args += ["-c", "shutdown"]
    r = subprocess.run(args, capture_output=True, text=True, timeout=180)
    return r.returncode, r.stdout + r.stderr


def word(text, address):
    m = re.search(r"0x%08x:\s+([0-9a-f]{8})" % address, text)
    return int(m.group(1), 16) if m else None


def info():
    for under_reset in (False, True):
        code, out = openocd(["mdw 0xE0042000", "mdw 0x1FFFF7E0", "mdw 0x4002201C", "mdw 0x1FFFF800 4"], under_reset)
        idcode = word(out, 0xE0042000)
        if idcode is not None:
            break
        print("Connection without reset failed, trying under reset ..." if not under_reset else "")
    if idcode is None:
        print(out[-2500:])
        sys.exit("No connection to the chip. Check the wiring, VCC and the mains plug (must be unplugged).")
    flash_kb = (word(out, 0x1FFFF7E0) or 0) & 0xFFFF
    obstat = word(out, 0x4002201C) or 0
    protected = bool(obstat & 0x2)
    print(f"DBGMCU IDCODE   : 0x{idcode:08x}")
    print(f"Flash           : {flash_kb} KB")
    print(f"OBSTAT          : 0x{obstat:08x}")
    m = re.search(r"0x1ffff800:\s+((?:[0-9a-f]{8} ?)+)", out)
    print("Option bytes    :", m.group(1).strip() if m else "?")
    print("Read protection :", "ACTIVE - flash cannot be read" if protected else "off - backup possible")
    return flash_kb, protected, under_reset


def backup():
    flash_kb, protected, under_reset = info()
    if protected:
        sys.exit("Read protection active: no backup possible. Nothing was changed.")
    os.makedirs(TARGET, exist_ok=True)
    size = flash_kb * 1024
    hashes = []
    for n in (1, 2):
        path = os.path.join(TARGET, f"read{n}.bin")
        code, out = openocd([f"dump_image {{{path.replace(os.sep, '/')}}} 0x08000000 {size}",
                             f"dump_image {{{os.path.join(TARGET, 'optionbytes.bin').replace(os.sep, '/')}}} 0x1FFFF800 16"],
                            under_reset)
        if not os.path.exists(path) or os.path.getsize(path) != size:
            print(out[-2500:])
            sys.exit(f"Read {n} failed.")
        hashes.append(hashlib.sha256(open(path, "rb").read()).hexdigest())
        print(f"Read {n}: {size} bytes, sha256 {hashes[-1][:16]}...")
    if hashes[0] != hashes[1]:
        sys.exit("The two reads differ - unreliable connection, please repeat.")
    final = os.path.join(TARGET, f"gd32f303cbt6-original-{flash_kb}KB.bin")
    os.replace(os.path.join(TARGET, "read1.bin"), final)
    os.remove(os.path.join(TARGET, "read2.bin"))
    empty = open(final, "rb").read().count(b"\xff") * 100 // size
    print(f"Saved: {final} ({empty} % empty/0xFF), sha256 {hashes[0]}")


if __name__ == "__main__":
    {"info": info, "backup": backup}.get(sys.argv[1] if len(sys.argv) > 1 else "info", info)()
