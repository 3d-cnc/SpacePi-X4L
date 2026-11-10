"""Segment-Zuordnung des X4L-Displays (YXD-BS0023-3) und Bildaufbau.

Ermittelt am 10.11.2026 aus 14 Fotos (fotos/segmente/foto01..14.jpg, Fotoserie der
Firmware 1.0.1) und gegengeprueft am Ruhebild der Original-Firmware.

Bild = 20 Byte, gesendet als 7E 00 b0..b19.

Normale Ziffern (1 Byte): 0x80 a (oben)   0x40 b (rechts oben)  0x20 c (rechts unten)
                          0x10 d (unten)  0x08 f (links oben)   0x04 g (Mitte)
                          0x02 e (links unten)                  0x01 Symbol
Uhrzeit T1..T6 (um ein halbes Byte versetzt):
    Byte k, obere Haelfte = linke Seite von T(k-1): 0x80 f, 0x40 g, 0x20 e, 0x10 Symbol
    Byte k, untere Haelfte = rechte Seite von T(k-2): 0x08 a, 0x04 b, 0x02 c, 0x01 d

Aufruf als Skript: python lcd_bild.py  -> druckt das Testbild als Hex.
"""

# Byte-Nummern
KLEIN_10, KLEIN_1 = 0, 1           # Solltemperatur (klein, neben Thermometer): Zehner, Einer
GROSS_10, GROSS_1 = 11, 12         # Temperatur gross: Zehner, Einer
FEUCHTE_10, FEUCHTE_1 = 9, 10
MATERIAL = (14, 15, 16, 17, 18, 19)

# Symbole: Name -> (Byte, Bit)
SYMBOLE = {
    "klein_hundert": (0, 0x01),    # "1" vor der kleinen Temperatur
    "thermometer": (1, 0x01),
    "L": (2, 0x08),
    "time": (2, 0x10),
    "klein_grad_oben": (2, 0x02),  # Grad-Zeichen + oberer/linker Teil des C
    "klein_c_unten": (2, 0x01),    # unterer Strich: mit grad_oben = °C
    "klein_c_mitte": (2, 0x04),    # mittlerer Strich: mit grad_oben = °F
    "zahnrad": (3, 0x10),
    "doppelpunkt1": (4, 0x10),
    "pfeil_hoch": (5, 0x10),
    "doppelpunkt2": (6, 0x10),
    "pfeil_runter": (7, 0x10),
    "ring_oben": (8, 0x80),
    "ring_oben_links": (8, 0x40),
    "ring_links": (8, 0x20),
    "ring_unten_links": (8, 0x10),
    "tropfen": (9, 0x01),
    "rh": (10, 0x01),
    "gross_hundert": (11, 0x01),   # "1" vor der grossen Temperatur
    "prozent": (12, 0x01),
    "ring_oben_rechts": (13, 0x80),
    "R": (13, 0x40),
    "ring_rechts": (13, 0x20),
    "ring_unten_rechts": (13, 0x10),
    "pv": (13, 0x08),
    "gross_c_mitte": (13, 0x04),
    "gross_grad_oben": (13, 0x02),
    "gross_c_unten": (13, 0x01),
    "material_14": (14, 0x01),     # unbekannt, auf den Fotos nichts gesehen
    "lr_taste": (15, 0x01),
    "on": (16, 0x01),
    "auto": (17, 0x01),
    "off": (18, 0x01),
    "material": (19, 0x01),
}

# 7-Segment-Zeichen als Menge aus a..g
ZEICHEN = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd",
    "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g", " ": "",
    "A": "abcefg", "B": "abcdefg", "b": "cdefg", "C": "adef", "c": "ged", "d": "bcdeg",
    "E": "adefg", "F": "aefg", "H": "bcefg", "h": "cefg", "L": "def", "n": "ceg",
    "o": "cdeg", "P": "abefg", "r": "eg", "S": "afgcd", "t": "defg", "U": "bcdef", "u": "cde",
    "G": "acdef", "I": "bc", "J": "bcde", "y": "bcdfg",
}
NORMAL = {"a": 0x80, "b": 0x40, "c": 0x20, "d": 0x10, "f": 0x08, "g": 0x04, "e": 0x02}
ZEIT_LINKS = {"f": 0x80, "g": 0x40, "e": 0x20}
ZEIT_RECHTS = {"a": 0x08, "b": 0x04, "c": 0x02, "d": 0x01}


def segmente(z):
    return ZEICHEN[z]


class Bild:
    def __init__(self):
        self.b = bytearray(20)

    def ziffer(self, byte, z):
        for s in segmente(z):
            self.b[byte] |= NORMAL[s]

    def symbol(self, name, an=True):
        byte, bit = SYMBOLE[name]
        if an:
            self.b[byte] |= bit
        else:
            self.b[byte] &= ~bit

    def zeit(self, text):
        """text = 6 Zeichen, z. B. '123456' fuer 12:34:56 (Doppelpunkte separat)."""
        for i, z in enumerate(text):          # Ziffer T(i+1)
            for s in segmente(z):
                if s in ZEIT_LINKS:
                    self.b[2 + i] |= ZEIT_LINKS[s]
                else:
                    self.b[3 + i] |= ZEIT_RECHTS[s]

    def hex(self):
        return self.b.hex()


def testbild():
    """Jede Ziffer zeigt etwas anderes, die Haelfte der Symbole ist an."""
    b = Bild()
    b.zeit("123456")
    b.symbol("doppelpunkt1"); b.symbol("time")              # Doppelpunkt 2 bleibt aus
    b.ziffer(KLEIN_10, "4"); b.ziffer(KLEIN_1, "5"); b.symbol("klein_hundert")
    b.symbol("klein_grad_oben"); b.symbol("klein_c_unten"); b.symbol("thermometer")
    b.ziffer(GROSS_10, "2"); b.ziffer(GROSS_1, "7")            # grosse Temperatur 27, ohne "1"
    b.symbol("gross_grad_oben"); b.symbol("gross_c_mitte")     # grosses °F (Mitte statt unten)
    b.ziffer(FEUCHTE_10, "3"); b.ziffer(FEUCHTE_1, "9"); b.symbol("rh")   # ohne Tropfen, ohne %
    for byte, z in zip(MATERIAL, "PLA-HS"):
        b.ziffer(byte, z)
    b.symbol("material"); b.symbol("on")                       # OFF und AUTO aus
    b.symbol("L"); b.symbol("pv")                              # R aus
    b.symbol("ring_oben"); b.symbol("ring_oben_rechts"); b.symbol("ring_rechts")   # nur 3 Ringteile
    b.symbol("zahnrad"); b.symbol("lr_taste")                  # Pfeiltasten aus
    return b


def lesen(bild):
    """Gibt in Worten zurueck, was ein 20-Byte-Bild auf dem Display zeigt."""
    b = bytes(bild)
    inv = {frozenset(v): k for k, v in ZEICHEN.items() if k not in "BIoS"}
    inv[frozenset(ZEICHEN["S"])] = "5"

    def norm(byte):
        s = frozenset(x for x, m in NORMAL.items() if b[byte] & m)
        return inv.get(s, "?") if s else " "

    def zeit(i):
        s = frozenset({x for x, m in ZEIT_LINKS.items() if b[2 + i] & m} | {x for x, m in ZEIT_RECHTS.items() if b[3 + i] & m})
        return inv.get(s, "?") if s else " "

    def an(name):
        byte, bit = SYMBOLE[name]
        return bool(b[byte] & bit)

    def grad(prefix):
        if not an(prefix + "_grad_oben"):
            return ""
        return "°C" if an(prefix + "_c_unten") else ("°F" if an(prefix + "_c_mitte") else "°?")

    t = "".join(zeit(i) for i in range(6))
    zeile = (f"Zeit {t[0:2]}{':' if an('doppelpunkt1') else ' '}{t[2:4]}{':' if an('doppelpunkt2') else ' '}{t[4:6]} | "
             f"Soll {'1' if an('klein_hundert') else ''}{norm(0)}{norm(1)}{grad('klein')} | "
             f"Ist {'1' if an('gross_hundert') else ''}{norm(11)}{norm(12)}{grad('gross')} | "
             f"Feuchte {norm(9)}{norm(10)}{'%' if an('prozent') else ''} | "
             f"Material '{''.join(norm(x) for x in MATERIAL)}'")
    ring = [n for n in ("ring_unten_links", "ring_links", "ring_oben_links", "ring_oben",
                        "ring_oben_rechts", "ring_rechts", "ring_unten_rechts") if an(n)]
    symbole = [n for n in ("L", "R", "on", "off", "auto", "pv") if an(n)]
    return zeile + f"\nSymbole: {' '.join(symbole)} | Ring {len(ring)}/7"


if __name__ == "__main__":
    print(testbild().hex())
    print(lesen(testbild().b))
