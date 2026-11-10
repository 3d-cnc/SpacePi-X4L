# SpacePi X4L – custom firmware with Home Assistant integration

Free replacement firmware for the **Creality SpacePi X4L** filament dryer (control board
"Dryer_Pro_V14", GD32F303CBT6). The **original display and the original keys stay**, the
hardware is not modified – you only add two pin headers and, optionally, an ESP32-C3.

## Features

- Two independent chambers: material presets, target temperature 35–75 °C, duration 0:30–48 h
- Controls the chamber air (AHT20), within ±0.3 K after 4 min, heats 25 → 45 °C in about 1.5 min
- **Never heats without the fan**: a heater output only switches on when the fan of the same
  chamber has verifiably been running for 3 s; the fan keeps running until the heater surface is below 45 °C
- Protection: the heater surface is derated from 110 °C and switched off at 120 °C, fault at 135 °C,
  chamber ≥ 80 °C, sensor failure, heater does not switch off / does not heat, watchdog
- Settings are stored, operation as usual with the four touch keys
- Optional **Home Assistant** through an ESP32-C3 with ESPHome: thermostat per chamber, material,
  duration, time remaining, humidity, heater surface, faults. Safety stays in the dryer – even when
  controlled remotely it only heats with the fan running and within all limits.

## What you need

To buy: a **ST-Link V2** (a clone will do) and, only for Home Assistant, an **ESP32-C3 SuperMini**.
Plus two pin headers, a few jumper wires, a soldering iron and a multimeter.
Full parts list and step-by-step guide: **[docs/GUIDE.md](docs/GUIDE.md)**.

## Contents

| Folder | Contents |
|---|---|
| `firmware/` | Firmware for the GD32F303 (C, no vendor libraries), `python firmware/build.py` |
| `esphome/` | ESPHome device for the ESP32-C3 with its own component `x4l` |
| `tools/` | Backup, flashing, status over SWD, heat and control tests, display map, NTC curve |
| `docs/` | Guide, hardware description, pinout, display protocol, photos |

## The original firmware is not included

The Creality firmware is protected by copyright and is **not** in this repository.
`tools/swd_backup.py` backs up the firmware of **your own device** before the conversion; only with
this backup can you go back later (`python tools/x4l.py restore`).
The pinout and the display protocol were worked out by measuring and examining my own device, in
order to write independent firmware that works with it.

## Protocol to the ESP

Line based over the service header J26, 115200 baud, with an XOR checksum; commands without a valid
checksum are discarded. Description: [`firmware/src/protocol.h`](firmware/src/protocol.h).

## Disclaimer

The dryer runs on mains voltage with 2 × 200 W of heating power. Conversion and operation at your
own risk; the manufacturer's warranty is void. Not an official Creality project.

## License

MIT – see [LICENSE](LICENSE).
