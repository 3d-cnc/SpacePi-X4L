# Anleitung: SpacePi X4L auf eigene Firmware umbauen

> ⚠ **Netzspannung und 2 × 200 W Heizleistung.** Gehäuse nur bei **gezogenem Netzstecker** öffnen.
> Der Umbau erfolgt auf eigenes Risiko und beendet die Herstellergarantie. Die Creality-Firmware
> lässt sich nur wiederherstellen, wenn du sie **vorher selbst gesichert** hast (Schritt 6).

Getestet mit: SpacePi **X4L**, Platine **Dryer_Pro_V14**, GD32F303CBT6. Andere Modelle (X4 ohne L,
andere Platinenstände) sind **nicht** getestet – vorher Platine und Pinbelegung vergleichen.

---

## Teileliste

### Kaufen

| Teil | Wofür | ca. Preis |
|---|---|---|
| **ST-Link V2** (Klon reicht), mit Buchse-Buchse-Kabeln | Firmware sichern und flashen (SWD) | 5–10 € |
| **ESP32-C3 SuperMini** | nur für WLAN / Home Assistant – ohne ESP läuft der Trockner eigenständig | 3–5 € |
| Stiftleiste 2,54 mm, gerade, 1-reihig (1 × 5 und 1 × 3 Pins) | J25 (SWD) und J26 (UART) haben nur Lötlöcher | < 1 € |
| Jumperkabel Buchse–Buchse, ~20 cm | 5 für den ST-Link (meist dabei), 4 für den ESP | 2 € |

### Werkzeug

- Lötkolben und Lot (Stiftleisten einlöten, beim ESP ggf. ebenfalls die Stiftleiste)
- Multimeter (Isolation prüfen, 5 V am Display-Stecker finden)
- Kreuzschlitz-Schraubendreher (Gehäuse)
- USB-C-**Daten**kabel für den ESP32-C3 (nur zum ersten Flashen)
- PC mit Windows oder Linux

### Software (kostenlos)

| Programm | Wofür |
|---|---|
| Python 3.10–3.12 | alle Werkzeuge in `werkzeuge/` |
| ARM-GCC (`arm-none-eabi-gcc`) | Firmware bauen |
| OpenOCD | ST-Link ansprechen |
| ESPHome (nur mit ESP) | ESP-Firmware bauen und flashen |
| Home Assistant (nur mit ESP) | Steuerung per WLAN |

ARM-GCC und OpenOCD gibt es bequem über PlatformIO:

```bash
pip install platformio
pio pkg install -g -t platformio/toolchain-gccarmnoneeabi -t platformio/tool-openocd
```

Die Skripte finden beides dort automatisch. Alternativ: eigene Installation im `PATH`
oder per Umgebungsvariable `ARM_GCC_BIN` (Ordner) bzw. `OPENOCD` (Datei).

> 💡 **Kein Logic Analyzer nötig.** Der wurde nur gebraucht, um das Gerät zu entschlüsseln.

---

## 1. Gehäuse öffnen, Platine prüfen

Netzstecker ziehen, Steuerplatine freilegen. Auf der Platine muss **„Dryer_Pro_V14“** stehen und der
Mikrocontroller **GD32F303CBT6** sein (Fotos in [HARDWARE.md](HARDWARE.md)). Wenn nicht: abbrechen.

## 2. Stiftleisten einlöten

- **J25** (SWD): 5 Pins – Beschriftung `RST GND CLK DIO VCC`
- **J26** (UART): 3 Pins – Beschriftung `TX RX GND`

## 3. Isolation messen

Netzstecker gezogen, Multimeter auf Durchgang/Widerstand. Eine Spitze an **GND von J26**, die andere
nacheinander an **L** und **N** der Netzklemme sowie an alle Schrauben von **PTC1/PTC2**:
überall muss **„offen“ (OL)** angezeigt werden. Sonst **nicht weitermachen**.

## 4. Software installieren

Repo herunterladen. **Der Pfad darf keine Leerzeichen enthalten** (ESP-IDF verweigert sonst das
Bauen), also z. B. `C:\SpacePi-X4L` statt `C:\Meine Projekte\…`.

## 5. ST-Link anschließen – **Netzstecker gezogen**

| ST-Link | J25 |
|---|---|
| 3.3V | VCC |
| GND | GND |
| SWDIO | DIO |
| SWCLK | CLK |

Der ST-Link versorgt die Platine dabei mit 3,3 V. **Nie mit gestecktem Netzstecker an der
Creality-Firmware arbeiten:** hält der Debugger den Chip an, während eine Heizung an ist, bleibt sie an.

## 6. Original-Firmware sichern

```bash
python werkzeuge/swd_sichern.py info      # nur lesen: Chip, Flashgröße, Leseschutz
python werkzeuge/swd_sichern.py sichern   # liest den Flash zweimal, vergleicht, speichert
```

Ergebnis: `sicherung/x4l-original/gd32f303cbt6-original-128KB.bin`. **Gut aufheben** – ohne diese
Datei gibt es keinen Weg zurück (sie ist aus rechtlichen Gründen nicht im Repo).
Meldet `info` einen aktiven Leseschutz, lässt sich das Original nicht sichern; dann entweder auf den
Rückweg verzichten oder hier aufhören.

## 7. Firmware bauen und flashen

```bash
python firmware/bauen.py
python werkzeuge/x4l.py flashen
python werkzeuge/x4l.py status           # Laufzeit, Sensoren, Display
```

Beim ersten Flashen läuft noch die Creality-Firmware – deshalb Netzstecker weiterhin gezogen.

## 8. Erster Test mit Netz

Ab jetzt läuft die eigene Firmware, die Heizungen schalten nur noch mit Lüfter. Zum Mitlesen darf der
ST-Link angeschlossen bleiben, **aber ohne die 3.3V-Leitung** (sonst arbeiten zwei Versorgungen gegeneinander).

1. Netzstecker rein – das Display zeigt Temperatur, Feuchte, Material, OFF.
2. `python werkzeuge/x4l.py status` – beide AHT20 liefern Werte, Fehler 0.
3. Lüftertest (je 2 s): `python werkzeuge/x4l.py luefter 1` (links), `… luefter 2` (rechts).
4. Heiztest: `python werkzeuge/heiztest.py` starten, dann am Gerät eine Kammer starten
   (Zahnrad 1,5 s halten). Das Skript überwacht 40 s und stoppt selbst; es bricht sofort ab, wenn
   eine Heizung ohne Lüfter liefe oder die falsche Kammer warm würde.
5. Optional Regeltest über 15 min: `python werkzeuge/regeltest.py` (protokolliert nach `protokolle/`).

## 9. ESP32-C3 einrichten (für Home Assistant)

1. `esphome/secrets.example.yaml` nach `esphome/secrets.yaml` kopieren und ausfüllen:
   WLAN (nur **2,4 GHz**), einen eigenen API-Schlüssel und ein OTA-Passwort. Einen Schlüssel erzeugt
   z. B. `python -c "import secrets,base64;print(base64.b64encode(secrets.token_bytes(32)).decode())"`.
2. ESP per USB anschließen und flashen:
   ```bash
   esphome run esphome/x4l-trockner.yaml
   ```
   Unter Windows **aus PowerShell oder cmd**, nicht aus Git-Bash/MSYS (ESP-IDF bricht dort ab).
3. Bleibt der ESP nach dem Flashen stumm (bekannt beim C3 mit USB): kurz vom USB trennen
   oder `python -m esptool --chip esp32c3 --port COMx --after watchdog-reset chip-id`.
4. Später gehen Updates per WLAN: `esphome run esphome/x4l-trockner.yaml --device x4l-trockner.local`.

## 10. ESP verdrahten – **Netzstecker gezogen**

| Trockner | ESP32-C3 SuperMini |
|---|---|
| J26 **TX** | **GPIO20** (RX) |
| J26 **RX** | **GPIO21** (TX) |
| J26 **GND** | **GND** |
| Display-Stecker J12 **Pin 3** (5 V) | **5V** |

- Stiftleisten am ESP **verlöten**, lose Jumper haben oft keinen Kontakt.
- **Nie gleichzeitig USB und die 5 V vom Trockner** anschließen (beim SuperMini direkt verbunden).
- Die 5 V kommen aus einem AMS1117 an 12 V. Mit dem Stromsparmodus des ESP (in der YAML gesetzt) wird
  er warm, aber nicht heiß. Nach 30 min Betrieb einmal anfassen; ist er zu heiß zum Anfassen, den ESP
  stattdessen über einen kleinen Step-Down-Wandler (12 V → 5 V) versorgen.

Test: Die Weboberfläche des ESP (`http://x4l-trockner.local`) zeigt „Verbindung zum Trockner: ON“,
Temperaturen beider Kammern und „Verworfene Zeilen: 0“.

## 11. Home Assistant

*Einstellungen → Geräte & Dienste* → das entdeckte Gerät **x4l-trockner** hinzufügen (sonst:
Integration *ESPHome*, Host `x4l-trockner.local` oder IP) → API-Schlüssel aus `secrets.yaml` eingeben.

Entitäten: Thermostate *Kammer links/rechts* (Heizen = Trocknen starten), Material, Dauer, Restzeit,
Temperatur, Feuchte, Heizfläche, Heizleistung, Lüfter, Störung + Klartext, *Fehler quittieren*.

---

## Bedienung am Gerät

| Taste | Wirkung |
|---|---|
| L/R | linke / rechte Kammer anzeigen |
| Zahnrad kurz | Feld wählen: Material → Solltemperatur → Zeit → fertig (Feld blinkt) |
| Pfeile | Wert ändern (halten = schnell) |
| Zahnrad 1,5 s | Trocknen der angezeigten Kammer starten / stoppen |

Material-Vorgaben: PLA 50 °C/6 h, PETG 65/6, ABS 75/6, ASA 75/6, TPU 55/8, PA 75/12, PC 75/8, USER.
Solltemperatur 35–75 °C, Dauer 0:30–48 h. Einstellungen bleiben nach dem Ausschalten erhalten.

## Fehlercodes („Err n“ im Materialfeld)

| Code | Bedeutung | Reaktion |
|---|---|---|
| 1 | Kammersensor (AHT20) liefert nichts | Kammer stoppt |
| 2 | Heizungsfühler (NTC) offen/kurz | Kammer stoppt |
| 3 | Heizfläche ≥ 135 °C | Kammer stoppt |
| 4 | Kammer ≥ 80 °C | Kammer stoppt |
| 5 | Heizung schaltet nicht ab | Lüfter laufen dauerhaft – **Netzstecker ziehen** |
| 6 | Heizung heizt nicht | Kammer stoppt |

Quittieren mit Zahnrad (bzw. in Home Assistant), Fehler 5 erst nach einem Neustart.

## Zurück zur Creality-Firmware

Netzstecker ziehen, ST-Link mit 3.3V anschließen:

```bash
python werkzeuge/x4l.py original      # nimmt sicherung/x4l-original/gd32f303cbt6-original-128KB.bin
```
