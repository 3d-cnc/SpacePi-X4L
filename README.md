# SpacePi X4L – eigene Firmware mit Home-Assistant-Anbindung

Freie Ersatz-Firmware für den Filamenttrockner **Creality SpacePi X4L** (Steuerplatine
„Dryer_Pro_V14“, GD32F303CBT6). Das **Original-Display und die Original-Tasten bleiben**, die
Hardware wird nicht verändert – es kommen nur zwei Stiftleisten und optional ein ESP32-C3 dazu.

## Was es kann

- Zwei Kammern unabhängig: Material-Vorgaben, Solltemperatur 35–75 °C, Dauer 0:30–48 h
- Regelung auf die Kammerluft (AHT20), nach 4 min auf ±0,3 K, Aufheizen 25 → 45 °C in ~1,5 min
- **Heizung nie ohne Lüfter**: der Heizungsausgang schaltet nur, wenn der Lüfter derselben Kammer
  nachweislich seit 3 s läuft; Lüfter-Nachlauf bis die Heizfläche unter 45 °C ist
- Schutz: Heizfläche wird ab 110 °C abgeregelt und ab 120 °C abgeschaltet, Fehler bei 135 °C,
  Kammer ≥ 80 °C, Sensorausfall, Heizung schaltet nicht ab / heizt nicht, Watchdog
- Einstellungen bleiben gespeichert, Bedienung wie gewohnt über die vier Touch-Tasten
- Optional **Home Assistant** über einen ESP32-C3 mit ESPHome: Thermostat je Kammer, Material,
  Dauer, Restzeit, Feuchte, Heizfläche, Störungen. Die Sicherheit bleibt im Trockner – auch
  ferngesteuert heizt er nur mit Lüfter und innerhalb aller Grenzen.

## Was du brauchst

Zu kaufen: **ST-Link V2** (Klon reicht) und, nur für Home Assistant, ein **ESP32-C3 SuperMini**.
Dazu zwei Stiftleisten, ein paar Jumperkabel, Lötkolben und Multimeter.
Vollständige Liste und Schritt-für-Schritt-Anleitung: **[docs/ANLEITUNG.md](docs/ANLEITUNG.md)**.

## Inhalt

| Ordner | Inhalt |
|---|---|
| `firmware/` | Firmware für den GD32F303 (C, ohne Herstellerbibliotheken), `python firmware/bauen.py` |
| `esphome/` | ESPHome-Gerät für den ESP32-C3 mit eigener Komponente `x4l` |
| `werkzeuge/` | Sichern, Flashen, Status per SWD, Heiz- und Regeltest, Display-Zuordnung, NTC-Kennlinie |
| `docs/` | Anleitung, Hardware-Beschreibung, Pinbelegung, Display-Protokoll, Fotos |

## Die Original-Firmware ist nicht enthalten

Die Creality-Firmware ist urheberrechtlich geschützt und liegt **nicht** in diesem Repo.
`werkzeuge/swd_sichern.py` sichert die Firmware **deines eigenen Geräts** vor dem Umbau; nur mit
dieser Sicherung kommst du später zurück (`python werkzeuge/x4l.py original`).
Die Pinbelegung und das Display-Protokoll wurden durch Messen und Untersuchen des eigenen Geräts
ermittelt, um eine eigene, damit zusammenarbeitende Firmware zu schreiben.

## Protokoll zum ESP

Zeilenbasiert über die Service-Leiste J26, 115200 Baud, mit XOR-Prüfsumme; Befehle ohne gültige
Prüfsumme werden verworfen. Beschreibung: [`firmware/src/protokoll.h`](firmware/src/protokoll.h).

## Haftung

Der Trockner arbeitet mit Netzspannung und 2 × 200 W Heizleistung. Umbau und Betrieb auf eigenes
Risiko; die Herstellergarantie erlischt. Kein offizielles Creality-Projekt.

## Lizenz

MIT – siehe [LICENSE](LICENSE).
