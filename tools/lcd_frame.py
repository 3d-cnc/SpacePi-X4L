"""Segment map of the X4L display (YXD-BS0023-3) and frame layout.

Worked out from a photo series of 14 test frames (test mode 8 of the firmware) and
cross-checked against the idle frame of the original firmware.

Frame = 20 bytes, sent as 7E 00 b0..b19.

Normal digits (1 byte): 0x80 a (top)     0x40 b (top right)    0x20 c (bottom right)
                        0x10 d (bottom)  0x08 f (top left)     0x04 g (middle)
                        0x02 e (bottom left)                   0x01 symbol
Clock T1..T6 (shifted by half a byte):
    byte k, upper half = left side of T(k-1):  0x80 f, 0x40 g, 0x20 e, 0x10 symbol
    byte k, lower half = right side of T(k-2): 0x08 a, 0x04 b, 0x02 c, 0x01 d

Run as a script: python tools/lcd_frame.py  -> prints the test frame as hex and decoded.
"""

# Byte numbers
SMALL_10, SMALL_1 = 0, 1           # setpoint (small, next to the thermometer): tens, ones
BIG_10, BIG_1 = 11, 12             # big temperature: tens, ones
HUMIDITY_10, HUMIDITY_1 = 9, 10
MATERIAL = (14, 15, 16, 17, 18, 19)

# Symbols: name -> (byte, bit)
SYMBOLS = {
    "small_hundred": (0, 0x01),    # "1" in front of the small temperature
    "thermometer": (1, 0x01),
    "L": (2, 0x08),
    "time": (2, 0x10),
    "small_degree": (2, 0x02),     # degree sign + upper/left part of the C
    "small_c_bottom": (2, 0x01),   # bottom stroke: with small_degree = °C
    "small_c_middle": (2, 0x04),   # middle stroke: with small_degree = °F
    "gear": (3, 0x10),
    "colon1": (4, 0x10),
    "arrow_up": (5, 0x10),
    "colon2": (6, 0x10),
    "arrow_down": (7, 0x10),
    "ring_top": (8, 0x80),
    "ring_top_left": (8, 0x40),
    "ring_left": (8, 0x20),
    "ring_bottom_left": (8, 0x10),
    "droplet": (9, 0x01),
    "rh": (10, 0x01),
    "big_hundred": (11, 0x01),     # "1" in front of the big temperature
    "percent": (12, 0x01),
    "ring_top_right": (13, 0x80),
    "R": (13, 0x40),
    "ring_right": (13, 0x20),
    "ring_bottom_right": (13, 0x10),
    "pv": (13, 0x08),
    "big_c_middle": (13, 0x04),
    "big_degree": (13, 0x02),
    "big_c_bottom": (13, 0x01),
    "material_14": (14, 0x01),     # unknown, nothing visible in the photos
    "lr_key": (15, 0x01),
    "on": (16, 0x01),
    "auto": (17, 0x01),
    "off": (18, 0x01),
    "material": (19, 0x01),
}

# 7-segment characters as a set of a..g
CHARACTERS = {
    "0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd",
    "6": "afgedc", "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g", " ": "",
    "A": "abcefg", "B": "abcdefg", "b": "cdefg", "C": "adef", "c": "ged", "d": "bcdeg",
    "E": "adefg", "F": "aefg", "H": "bcefg", "h": "cefg", "L": "def", "n": "ceg",
    "o": "cdeg", "P": "abefg", "r": "eg", "S": "afgcd", "t": "defg", "U": "bcdef", "u": "cde",
    "G": "acdef", "I": "bc", "J": "bcde", "y": "bcdfg",
}
NORMAL = {"a": 0x80, "b": 0x40, "c": 0x20, "d": 0x10, "f": 0x08, "g": 0x04, "e": 0x02}
TIME_LEFT = {"f": 0x80, "g": 0x40, "e": 0x20}
TIME_RIGHT = {"a": 0x08, "b": 0x04, "c": 0x02, "d": 0x01}


def segments(ch):
    return CHARACTERS[ch]


class Frame:
    def __init__(self):
        self.b = bytearray(20)

    def digit(self, byte, ch):
        for s in segments(ch):
            self.b[byte] |= NORMAL[s]

    def symbol(self, name, on=True):
        byte, bit = SYMBOLS[name]
        if on:
            self.b[byte] |= bit
        else:
            self.b[byte] &= ~bit

    def time(self, text):
        """text = 6 characters, e.g. '123456' for 12:34:56 (colons separately)."""
        for i, ch in enumerate(text):         # digit T(i+1)
            for s in segments(ch):
                if s in TIME_LEFT:
                    self.b[2 + i] |= TIME_LEFT[s]
                else:
                    self.b[3 + i] |= TIME_RIGHT[s]

    def hex(self):
        return self.b.hex()


def test_frame():
    """Every digit shows something different, half of the symbols are on."""
    f = Frame()
    f.time("123456")
    f.symbol("colon1"); f.symbol("time")                        # colon 2 stays off
    f.digit(SMALL_10, "4"); f.digit(SMALL_1, "5"); f.symbol("small_hundred")
    f.symbol("small_degree"); f.symbol("small_c_bottom"); f.symbol("thermometer")
    f.digit(BIG_10, "2"); f.digit(BIG_1, "7")                   # big temperature 27, without "1"
    f.symbol("big_degree"); f.symbol("big_c_middle")            # big °F (middle instead of bottom)
    f.digit(HUMIDITY_10, "3"); f.digit(HUMIDITY_1, "9"); f.symbol("rh")   # no droplet, no %
    for byte, ch in zip(MATERIAL, "PLA-HS"):
        f.digit(byte, ch)
    f.symbol("material"); f.symbol("on")                        # OFF and AUTO off
    f.symbol("L"); f.symbol("pv")                               # R off
    f.symbol("ring_top"); f.symbol("ring_top_right"); f.symbol("ring_right")   # only 3 ring parts
    f.symbol("gear"); f.symbol("lr_key")                        # arrow keys off
    return f


def decode(frame):
    """Returns in words what a 20-byte frame shows on the display."""
    b = bytes(frame)
    inv = {frozenset(v): k for k, v in CHARACTERS.items() if k not in "BIoS"}
    inv[frozenset(CHARACTERS["S"])] = "5"

    def normal(byte):
        s = frozenset(x for x, m in NORMAL.items() if b[byte] & m)
        return inv.get(s, "?") if s else " "

    def clock(i):
        s = frozenset({x for x, m in TIME_LEFT.items() if b[2 + i] & m} | {x for x, m in TIME_RIGHT.items() if b[3 + i] & m})
        return inv.get(s, "?") if s else " "

    def on(name):
        byte, bit = SYMBOLS[name]
        return bool(b[byte] & bit)

    def degree(prefix):
        if not on(prefix + "_degree"):
            return ""
        return "°C" if on(prefix + "_c_bottom") else ("°F" if on(prefix + "_c_middle") else "°?")

    t = "".join(clock(i) for i in range(6))
    line = (f"time {t[0:2]}{':' if on('colon1') else ' '}{t[2:4]}{':' if on('colon2') else ' '}{t[4:6]} | "
            f"setpoint {'1' if on('small_hundred') else ''}{normal(0)}{normal(1)}{degree('small')} | "
            f"actual {'1' if on('big_hundred') else ''}{normal(11)}{normal(12)}{degree('big')} | "
            f"humidity {normal(9)}{normal(10)}{'%' if on('percent') else ''} | "
            f"material '{''.join(normal(x) for x in MATERIAL)}'")
    ring = [n for n in ("ring_bottom_left", "ring_left", "ring_top_left", "ring_top",
                        "ring_top_right", "ring_right", "ring_bottom_right") if on(n)]
    symbols = [n for n in ("L", "R", "on", "off", "auto", "pv") if on(n)]
    return line + f"\nsymbols: {' '.join(symbols)} | ring {len(ring)}/7"


if __name__ == "__main__":
    print(test_frame().hex())
    print(decode(test_frame().b))
