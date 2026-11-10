"""Watches a heat test of the X4L firmware (1.2.0 or later) and stops at once over SWD when:
  - the heater of a chamber is on without its fan running (must never happen),
  - the heater surface (NTC) of a chamber rises above NTC_LIMIT,
  - while heating one chamber the NTC of the OTHER chamber rises by more than 3 K
    (then the mapping heater <-> chamber would be wrong),
  - the test duration DURATION_S is reached.

  python tools/heat_test.py [duration_s] [ntc_limit]     (default 40 s, 70 °C)
The test only begins once a chamber has been started on the dryer (gear key 1.5 s).
"""
import sys
import time

sys.path.insert(0, __file__.replace("\\", "/").rsplit("/", 1)[0])
import swd_backup as swd  # noqa: E402
import x4l  # noqa: E402

DURATION_S = float(sys.argv[1]) if len(sys.argv) > 1 else 40
NTC_LIMIT = float(sys.argv[2]) if len(sys.argv) > 2 else 70.0


def read():
    w = x4l.read_words(x4l.STATUS, 53)
    s = lambda i: w[i] - (1 << 32) if w[i] & 0x80000000 else w[i]  # noqa: E731
    return {
        "running": w[33:35], "fault": w[43:45], "power": w[45:47],
        "ntc": [s(47) / 10, s(48) / 10], "fan": w[49:51], "heater": w[51:53],
        "aht": [s(10) / 100, s(11) / 100],
    }


def stop(reason):
    swd.openocd([f"mww 0x{x4l.CONTROL + 0x24:08x} 1"])
    print(f"\n*** STOPPED: {reason}", flush=True)


w = x4l.read_words(x4l.STATUS, 2)
if w[0] != 0x314C3458 or w[1] < 0x00010200:
    sys.exit("Firmware 1.2.0 or later required.")
print(f"Waiting for a chamber to start (test {DURATION_S:.0f} s, heater surface limit {NTC_LIMIT:.0f} °C) ...", flush=True)
while not any(read()["running"]):
    time.sleep(1)
d = read()
k = 0 if d["running"][0] else 1
other = 1 - k
start, ntc0 = time.time(), d["ntc"][:]
print(f"Chamber {'LR'[k]} running. NTC before L {ntc0[0]:.1f} / R {ntc0[1]:.1f} °C", flush=True)
reason = None
while reason is None:
    d = read()
    dt = time.time() - start
    print(f"{dt:5.1f} s  fan L/R {d['fan']}  heater L/R {d['heater']}  power {d['power']}  "
          f"NTC L {d['ntc'][0]:6.1f} R {d['ntc'][1]:6.1f}  chamber L {d['aht'][0]:.1f} R {d['aht'][1]:.1f}  fault {d['fault']}",
          flush=True)
    for i in (0, 1):
        if d["heater"][i] and not d["fan"][i]:
            reason = f"heater {'LR'[i]} on without fan!"
        if d["ntc"][i] >= NTC_LIMIT:
            reason = f"heater surface {'LR'[i]} {d['ntc'][i]:.1f} °C >= {NTC_LIMIT:.0f} °C"
    if d["ntc"][other] - ntc0[other] > 3:
        reason = f"wrong chamber is warming up ({'LR'[other]})"
    if not d["running"][k]:
        reason = f"chamber stopped by itself, fault {d['fault']}"
    if dt >= DURATION_S:
        reason = "test duration reached"
    if reason is None:
        time.sleep(1)
if "by itself" not in reason:
    stop(reason)
else:
    print("\n*** " + reason, flush=True)
time.sleep(2)
d = read()
print(f"After: heater L/R {d['heater']}  fan L/R {d['fan']} (after-run)  NTC L {d['ntc'][0]:.1f} R {d['ntc'][1]:.1f}", flush=True)
