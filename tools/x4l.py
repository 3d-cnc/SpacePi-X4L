"""Tool for the custom X4L firmware over the ST-Link (SWD).

  python tools/x4l.py flash            write the custom firmware (firmware/build/x4l.elf)
  python tools/x4l.py restore [FILE]   write your own backup of the Creality firmware back
                                       (default: backup/x4l-original/gd32f303cbt6-original-128KB.bin)
  python tools/x4l.py status           read the status of the running custom firmware
  python tools/x4l.py watch            status every second (Ctrl+C ends)
  python tools/x4l.py mode M [N]       display: 0 user interface, 9 sample frame, 1 all off,
                                       2 all on, 3 only byte N, 4 only bit N, 5 raw data
  python tools/x4l.py raw HEX          show 20 bytes of raw data (sets mode 5)
  python tools/x4l.py stop             stop all chambers immediately (1.2.0 or later)
  python tools/x4l.py fan 0..3         fan test (bit0 PB0, bit1 PB1), switches off after 2 s (firmware 1.0.2 or later)
                                       WARNING firmware 1.0.0/1.0.1: there this command switches PA9/PA10 = HEATERS!

For the first flash (Creality firmware still running) unplug the dryer and connect the ST-Link
with VCC. With the custom firmware running the dryer may stay plugged in, but then WITHOUT
the VCC wire of the ST-Link.
"""
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import swd_backup as swd  # noqa: E402

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(PROJECT, "firmware", "build", "x4l.elf")
ORIGINAL = os.path.join(PROJECT, "backup", "x4l-original", "gd32f303cbt6-original-128KB.bin")
STATUS, CONTROL = 0x20000000, 0x20000100


def path_arg(p):
    return "{" + p.replace(os.sep, "/") + "}"


def check_output(code, out, word):
    if word not in out:
        print(out[-3000:])
        sys.exit("Failed.")


def read_words(address, count):
    code, out = swd.openocd([f"mdw 0x{address:08x} {count}"])
    words = []
    for line in out.splitlines():
        m = re.match(r"0x([0-9a-f]{8}):((?:\s+[0-9a-f]{8})+)", line)
        if m:
            words += [int(w, 16) for w in m.group(2).split()]
    if len(words) < count:
        print(out[-2000:])
        sys.exit("Read failed - is the ST-Link connected?")
    return words[:count]


def status_text():
    w = read_words(STATUS, 31)
    s = read_words(CONTROL, 9)
    if w[0] != 0x314C3458:
        return f"Custom firmware not running (magic 0x{w[0]:08x})."
    b = struct.pack("<31I", *w)
    (magic, version, ms, clock, reset, keys, c0, c1, c2, c3, t0, t1, rh0, rh1, ok0, ok1, e0, e1,
     as0, as1, ntc0, ntc1, frames, nack, fan, left) = struct.unpack_from("<6I4I2i2i2I2I2I2I2I2I", b)
    frame = b[104:124]
    lines = [
        f"Firmware {version >> 16}.{version >> 8 & 255}.{version & 255}, uptime {ms / 1000:.1f} s, clock {clock / 1e6:.0f} MHz, reset cause 0x{reset:08x}",
        f"Keys now {keys:04b} (PA5..PA2), presses PA2={c0} PA3={c1} PA4={c2} PA5={c3}",
    ]
    for k, (t, rh, ok, e, a) in enumerate(((t0, rh0, ok0, e0, as0), (t1, rh1, ok1, e1, as1)), 1):
        state = "no answer" if a == 0xFFFFFFFF else f"status 0x{a:02x}"
        value = f"{t / 100:.2f} °C  {rh / 100:.2f} %" if ok else "--"
        lines.append(f"AHT20 bus {k} (PB{10 + 2 * k}/PB{11 + 2 * k}): {value}  ok={ok} errors={e} {state}")
    lines.append(f"NTC PA0={ntc0}  PA1={ntc1}  (ADC 0..4095, {ntc0 * 3.3 / 4095:.3f} V / {ntc1 * 3.3 / 4095:.3f} V)")
    lines.append(f"Display: frames={frames} NACK={nack}  mode={s[1]} n={s[2]}  frame={frame.hex(' ')}")
    lines.append(f"Fan test: {fan} ({left / 1000:.1f} s left)")
    try:
        import lcd_frame
        lines.append("Display shows: " + lcd_frame.decode(frame).replace("\n", "\n               "))
    except Exception as error:  # noqa: BLE001
        lines.append(f"Display: cannot decode ({error})")
    if version >= 0x00010100:
        u = read_words(STATUS + 124, 12)
        fields = ["-", "material", "temperature", "time"]
        lines.append(f"User interface: view {'LR'[u[0]]}, field {fields[u[1]] if u[1] < 4 else u[1]}")
        for k in range(2):
            state = f"running, {u[4 + k] // 3600}:{u[4 + k] // 60 % 60:02d}:{u[4 + k] % 60:02d} left" if u[2 + k] else "off"
            lines.append(f"  Chamber {'LR'[k]}: {state}, setpoint {u[6 + k]} °C, duration {u[8 + k] // 60}:{u[8 + k] % 60:02d} h, material {u[10 + k]}")
    if version >= 0x00010200:
        h = read_words(STATUS + 172, 10)
        ntc = [(v - (1 << 32) if v & 0x80000000 else v) / 10 for v in h[4:6]]
        for k in range(2):
            lines.append(f"  Heater {'LR'[k]}: fault {h[k]}, power {h[2 + k]} %, heater surface {ntc[k]:.1f} °C, "
                         f"fan {'on' if h[6 + k] else 'off'}, heater {'ON' if h[8 + k] else 'off'}")
    if version >= 0x00010300:
        c = read_words(STATUS + 212, 2)
        lines.append(f"UART: {c[0]} commands executed, {c[1]} lines rejected")
    return "\n".join(lines)


def write_word(address, value):
    code, out = swd.openocd([f"mww 0x{address:08x} 0x{value:08x}"])
    if "rror" in out and "Info" not in out:
        print(out[-2000:])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    command = sys.argv[1]
    if command == "flash":
        if not os.path.exists(ELF):
            sys.exit("Build first: python firmware/build.py")
        code, out = swd.openocd([f"program {path_arg(ELF)} verify reset"])
        check_output(code, out, "Verified OK")
        print("Written and verified, firmware is starting.")
    elif command == "restore":
        path = sys.argv[2] if len(sys.argv) > 2 else ORIGINAL
        if not os.path.exists(path):
            sys.exit(f"No backup found: {path}\n(It is created with 'python tools/swd_backup.py backup' "
                     "before the first flash. The Creality firmware is not in the repository for legal reasons.)")
        if os.path.getsize(path) != 131072:
            sys.exit(f"{path} is not 128 KB - not a complete backup.")
        code, out = swd.openocd([f"program {path_arg(path)} 0x08000000 verify reset"])
        check_output(code, out, "Verified OK")
        print("Creality firmware written back and verified.")
    elif command == "status":
        print(status_text())
    elif command == "watch":
        try:
            while True:
                print(status_text())
                print("-" * 60)
                time.sleep(1)
        except KeyboardInterrupt:
            pass
    elif command == "mode":
        if len(sys.argv) > 3:
            write_word(CONTROL + 8, int(sys.argv[3], 0))
        write_word(CONTROL + 4, int(sys.argv[2], 0))
        print(status_text())
    elif command == "raw":
        data = bytes.fromhex(sys.argv[2].replace(" ", ""))
        if len(data) != 20:
            sys.exit("Give exactly 20 bytes.")
        cmds = [f"mww 0x{CONTROL + 16 + 4 * i:08x} 0x{struct.unpack_from('<I', data, 4 * i)[0]:08x}" for i in range(5)]
        cmds.append(f"mww 0x{CONTROL + 4:08x} 5")
        swd.openocd(cmds)
        print(status_text())
    elif command == "stop":
        swd.openocd([f"mww 0x{CONTROL + 0x24:08x} 1"])
        print(status_text())
    elif command == "fan":
        magic, version = read_words(STATUS, 2)
        if magic != 0x314C3458 or version < 0x00010002:
            # Before 1.0.2 this control word switched PA9/PA10 = the HEATERS.
            sys.exit(f"Refused: firmware 0x{version:08x} is older than 1.0.2 - flash first.")
        write_word(CONTROL + 12, int(sys.argv[2], 0) & 3)
        print(status_text())
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
