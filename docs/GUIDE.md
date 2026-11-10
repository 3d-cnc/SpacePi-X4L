# Guide: converting the SpacePi X4L to custom firmware

> ⚠ **Mains voltage and 2 × 200 W of heating power.** Only open the case with the **dryer unplugged**.
> The conversion is at your own risk and ends the manufacturer's warranty. The Creality firmware
> can only be restored if you **backed it up yourself first** (step 6).

Tested with: SpacePi **X4L**, board **Dryer_Pro_V14**, GD32F303CBT6. Other models (X4 without L,
other board revisions) are **not** tested – compare the board and the pinout first.

---

## Parts list

### To buy

| Part | Used for | approx. price |
|---|---|---|
| **ST-Link V2** (a clone will do), with female-female wires | backing up and flashing the firmware (SWD) | 5–10 € |
| **ESP32-C3 SuperMini** | only for Wi-Fi / Home Assistant – without an ESP the dryer works on its own | 3–5 € |
| Pin header 2.54 mm, straight, single row (1 × 5 and 1 × 3 pins) | J25 (SWD) and J26 (UART) only have solder holes | < 1 € |
| Jumper wires female–female, ~20 cm | 5 for the ST-Link (usually included), 4 for the ESP | 2 € |

### Tools

- Soldering iron and solder (pin headers, possibly also the header on the ESP)
- Multimeter (check isolation, find 5 V on the display connector)
- Phillips screwdriver (case)
- USB-C **data** cable for the ESP32-C3 (only for the first flash)
- PC with Windows or Linux

### Software (free)

| Program | Used for |
|---|---|
| Python 3.10–3.12 | all scripts in `tools/` |
| ARM GCC (`arm-none-eabi-gcc`) | building the firmware |
| OpenOCD | talking to the ST-Link |
| ESPHome (only with ESP) | building and flashing the ESP firmware |
| Home Assistant (only with ESP) | control over Wi-Fi |

ARM GCC and OpenOCD are easiest to get through PlatformIO:

```bash
pip install platformio
pio pkg install -g -t platformio/toolchain-gccarmnoneeabi -t platformio/tool-openocd
```

The scripts find both there automatically. Alternatively: your own installation in the `PATH`
or through the environment variable `ARM_GCC_BIN` (folder) or `OPENOCD` (file).

> 💡 **No logic analyzer needed.** It was only needed to work out how the device works.

---

## 1. Open the case, check the board

Unplug the dryer and expose the control board. The board must say **"Dryer_Pro_V14"** and the
microcontroller must be a **GD32F303CBT6** (photos in [HARDWARE.md](HARDWARE.md)). If not: stop here.

## 2. Solder in the pin headers

- **J25** (SWD): 5 pins – labelled `RST GND CLK DIO VCC`
- **J26** (UART): 3 pins – labelled `TX RX GND`

## 3. Measure the isolation

Dryer unplugged, multimeter on continuity/resistance. One probe on **GND of J26**, the other one
in turn on **L** and **N** of the mains terminal and on all screws of **PTC1/PTC2**:
it must read **open (OL)** everywhere. Otherwise **do not continue**.

## 4. Install the software

Download the repository. **The path must not contain spaces** (ESP-IDF refuses to build otherwise),
e.g. `C:\SpacePi-X4L` instead of `C:\My Projects\…`.

## 5. Connect the ST-Link – **dryer unplugged**

| ST-Link | J25 |
|---|---|
| 3.3V | VCC |
| GND | GND |
| SWDIO | DIO |
| SWCLK | CLK |

The ST-Link supplies the board with 3.3 V. **Never work on the Creality firmware with the dryer
plugged in:** if the debugger halts the chip while a heater is on, it stays on.

## 6. Back up the original firmware

```bash
python tools/swd_backup.py info      # read only: chip, flash size, read protection
python tools/swd_backup.py backup    # reads the flash twice, compares, saves
```

Result: `backup/x4l-original/gd32f303cbt6-original-128KB.bin`. **Keep it safe** – without this file
there is no way back (it is not in the repository for legal reasons).
If `info` reports active read protection, the original cannot be backed up; then either give up
the way back or stop here.

## 7. Build and flash the firmware

```bash
python firmware/build.py
python tools/x4l.py flash
python tools/x4l.py status           # uptime, sensors, display
```

During the first flash the Creality firmware is still running – so keep the dryer unplugged.

## 8. First test on mains

From now on the custom firmware is running and the heaters only switch on with the fan. The ST-Link
may stay connected to watch, **but without the 3.3V wire** (otherwise two supplies work against each other).

1. Plug the dryer in – the display shows temperature, humidity, material, OFF.
2. `python tools/x4l.py status` – both AHT20 deliver readings, fault 0.
3. Fan test (2 s each): `python tools/x4l.py fan 1` (left), `… fan 2` (right).
4. Heat test: start `python tools/heat_test.py`, then start a chamber on the dryer
   (hold the gear key for 1.5 s). The script watches for 40 s and stops by itself; it aborts at once
   if a heater were on without its fan or the wrong chamber warmed up.
5. Optional control test over 15 min: `python tools/control_test.py` (logs to `logs/`).

## 9. Set up the ESP32-C3 (for Home Assistant)

1. Copy `esphome/secrets.example.yaml` to `esphome/secrets.yaml` and fill it in:
   Wi-Fi (**2.4 GHz** only), your own API key and an OTA password. A key can be generated with
   e.g. `python -c "import secrets,base64;print(base64.b64encode(secrets.token_bytes(32)).decode())"`.
2. Connect the ESP over USB and flash it:
   ```bash
   esphome run esphome/x4l-dryer.yaml
   ```
   On Windows **from PowerShell or cmd**, not from Git Bash/MSYS (ESP-IDF aborts there).
3. If the ESP stays silent after flashing (known with the C3 over USB): briefly unplug the USB
   or run `python -m esptool --chip esp32c3 --port COMx --after watchdog-reset chip-id`.
4. Later updates go over Wi-Fi: `esphome run esphome/x4l-dryer.yaml --device x4l-dryer.local`.

## 10. Wire up the ESP – **dryer unplugged**

| Dryer | ESP32-C3 SuperMini |
|---|---|
| J26 **TX** | **GPIO20** (RX) |
| J26 **RX** | **GPIO21** (TX) |
| J26 **GND** | **GND** |
| display connector J12 **pin 3** (5 V) | **5V** |

- **Solder** the pin header on the ESP, loose jumpers often have no contact.
- **Never connect USB and the 5 V from the dryer at the same time** (directly connected on the SuperMini).
- The 5 V come from an AMS1117 on 12 V. With the ESP's power save mode (set in the YAML) it gets
  warm, but not hot. Touch it once after 30 min of operation; if it is too hot to touch, supply the
  ESP from a small buck converter (12 V → 5 V) instead.

Test: the ESP's web page (`http://x4l-dryer.local`) shows "Connection to dryer: ON",
the temperatures of both chambers and "Rejected lines: 0".

## 11. Home Assistant

*Settings → Devices & services* → add the discovered device **x4l-dryer** (otherwise:
integration *ESPHome*, host `x4l-dryer.local` or the IP) → enter the API key from `secrets.yaml`.

Entities: thermostats *Left/Right chamber* (heat = start drying), material, duration, time remaining,
temperature, humidity, heater surface, heater power, fan, problem + plain-text fault, *acknowledge fault*.

---

## Operation on the device

| Key | Effect |
|---|---|
| L/R | show the left / right chamber |
| gear, short | select a field: material → target temperature → time → done (the field blinks) |
| arrows | change the value (hold = fast) |
| gear, 1.5 s | start / stop drying of the chamber shown |

Material presets: PLA 50 °C/6 h, PETG 65/6, ABS 75/6, ASA 75/6, TPU 55/8, PA 75/12, PC 75/8, USER.
Target temperature 35–75 °C, duration 0:30–48 h. Settings are kept after switching off.

## Fault codes ("Err n" in the material field)

| Code | Meaning | Reaction |
|---|---|---|
| 1 | chamber sensor (AHT20) delivers nothing | chamber stops |
| 2 | heater sensor (NTC) open/shorted | chamber stops |
| 3 | heater surface ≥ 135 °C | chamber stops |
| 4 | chamber ≥ 80 °C | chamber stops |
| 5 | heater does not switch off | fans keep running – **unplug the dryer** |
| 6 | heater does not heat | chamber stops |

Acknowledge with the gear key (or in Home Assistant), fault 5 only after a restart.

## Back to the Creality firmware

Unplug the dryer, connect the ST-Link with 3.3V:

```bash
python tools/x4l.py restore      # uses backup/x4l-original/gd32f303cbt6-original-128KB.bin
```
