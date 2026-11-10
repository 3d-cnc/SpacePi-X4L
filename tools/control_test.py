"""Control test of the X4L firmware (1.2.0 or later): logs every 2 s to CSV and stops over SWD on
  - heater on without fan (must never happen)
  - heater surface >= NTC_LIMIT (the firmware derates from 110 °C, off at 120 °C)
  - chamber air > setpoint + 8 K
  - other chamber > 8 K warmer than at the start
  - test duration reached

  python tools/control_test.py [duration_s=900] [ntc_limit=125]
Waits until a chamber is started on the dryer.
"""
import csv
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swd_backup as swd  # noqa: E402
import x4l  # noqa: E402

DURATION_S = float(sys.argv[1]) if len(sys.argv) > 1 else 900
NTC_LIMIT = float(sys.argv[2]) if len(sys.argv) > 2 else 125.0


def read():
    w = x4l.read_words(x4l.STATUS, 53)
    s = lambda i: w[i] - (1 << 32) if w[i] & 0x80000000 else w[i]  # noqa: E731
    return {"running": w[33:35], "setpoint": w[37:39], "fault": w[43:45], "power": w[45:47],
            "ntc": [s(47) / 10, s(48) / 10], "fan": w[49:51], "heater": w[51:53],
            "aht": [s(10) / 100, s(11) / 100], "rh": [s(12) / 100, s(13) / 100]}


def stop():
    swd.openocd([f"mww 0x{x4l.CONTROL + 0x24:08x} 1"])


w = x4l.read_words(x4l.STATUS, 2)
if w[0] != 0x314C3458 or w[1] < 0x00010200:
    sys.exit("Firmware 1.2.0 or later required.")
print(f"Waiting for a chamber to start (test {DURATION_S / 60:.0f} min) ...", flush=True)
while not any(read()["running"]):
    time.sleep(1)
d = read()
k = 0 if d["running"][0] else 1
a = 1 - k
setpoint = d["setpoint"][k]
start, aht_a0 = time.time(), d["aht"][a]
path = os.path.join(os.path.dirname(HERE), "logs", time.strftime(f"control-test-{'LR'[k]}-%Y%m%d-%H%M.csv"))
os.makedirs(os.path.dirname(path), exist_ok=True)
print(f"Chamber {'LR'[k]} running, setpoint {setpoint} °C. Log: {path}", flush=True)
reason, last_print, max_ntc, max_chamber = None, -30, 0, 0
with open(path, "w", newline="", encoding="utf-8") as f:
    out = csv.writer(f, delimiter=";")
    out.writerow(["s", "chamber_C", "setpoint_C", "heater_surface_C", "power_%", "heater", "fan", "rh_%", "other_chamber_C", "fault"])
    while reason is None:
        d = read()
        dt = time.time() - start
        out.writerow([f"{dt:.1f}", d["aht"][k], setpoint, d["ntc"][k], d["power"][k], d["heater"][k], d["fan"][k],
                      d["rh"][k], d["aht"][a], d["fault"][k]])
        f.flush()
        max_ntc, max_chamber = max(max_ntc, d["ntc"][k]), max(max_chamber, d["aht"][k])
        if dt - last_print >= 30:
            last_print = dt
            print(f"{dt / 60:5.1f} min  chamber {d['aht'][k]:5.1f} °C (setpoint {setpoint})  heater surface {d['ntc'][k]:5.1f} °C  "
                  f"power {d['power'][k]:3d} %  humidity {d['rh'][k]:4.1f} %  max surface {max_ntc:.1f}", flush=True)
        for i in (0, 1):
            if d["heater"][i] and not d["fan"][i]:
                reason = f"heater {'LR'[i]} on without fan!"
            if d["ntc"][i] >= NTC_LIMIT:
                reason = f"heater surface {'LR'[i]} {d['ntc'][i]:.1f} °C"
        if d["aht"][k] > setpoint + 8:
            reason = f"chamber {d['aht'][k]:.1f} °C > setpoint + 8 K"
        if d["aht"][a] - aht_a0 > 8:
            reason = "other chamber is warming up"
        if not d["running"][k]:
            reason = f"chamber stopped by itself, fault {d['fault']}"
        if dt >= DURATION_S:
            reason = "test duration reached"
        if reason is None:
            time.sleep(2)
if "by itself" not in reason:
    stop()
print(f"\n*** End: {reason}.  Max. chamber {max_chamber:.1f} °C, max. heater surface {max_ntc:.1f} °C", flush=True)
