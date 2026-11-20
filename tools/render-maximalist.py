from datetime import datetime
from functools import lru_cache
from math import cos, sin, pi
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build/face-previews"
NATIVE = ROOT / "build/face-previews/native"
FONTS = ROOT / "assets/fonts"
FONT_FILES = {"mono": "GeistMono.ttf", "sans": "Geist.ttf",
              "clock": "BarlowSemiCondensed-Bold.ttf"}

WHITE = 1
BLACK = 0
BATTERY = 65
STEPS = "4,286"
ALARM = "07:00"


@lru_cache(maxsize=None)
def font(face, size, weight):
    result = ImageFont.truetype(str(FONTS / FONT_FILES[face]), size)
    if face != "clock":
        result.set_variation_by_axes([weight])
    return result


class Face:
    def __init__(self):
        self.image = Image.new("1", (200, 200), WHITE)
        self.draw = ImageDraw.Draw(self.image)
        self.draw.fontmode = "1"

    def text(self, x, y, value, size, *, face="sans", weight=700, align="left", ink=BLACK):
        chosen = font(face, size, weight)
        glyph = chosen.getmask(value, mode="1")
        box = glyph.getbbox()
        if box is None:
            return
        image = Image.new("1", glyph.size, BLACK)
        image.putdata(list(glyph))
        mask = image.crop(box)
        if align == "middle":
            x -= mask.width // 2
        elif align == "right":
            x -= mask.width
        assert 0 <= x and x + mask.width <= 200 and 0 <= y and y + mask.height <= 200, (
            value, x, y, mask.size,
        )
        self.image.paste(ink, (x, y, x + mask.width, y + mask.height), mask)

    def rule(self, x0, y0, x1, y1, *, width=1, ink=BLACK):
        self.draw.line((x0, y0, x1, y1), fill=ink, width=width)

    def box(self, x0, y0, x1, y1, *, ink=BLACK, width=0):
        if width:
            self.draw.rectangle((x0, y0, x1, y1), outline=ink, width=width)
        else:
            self.draw.rectangle((x0, y0, x1, y1), fill=ink)

    def menu(self, x, y, *, ink=BLACK):
        self.draw.line([(x + 4, y), (x, y + 4), (x + 4, y + 8)], fill=ink, width=2)
        self.text(x + 10, y, "MENU", 12, face="mono", weight=700, ink=ink)

    def digits(self, at, y, size, centers, *, hour12, ink=BLACK, face="clock"):
        for digit, slot in zip(time_digits(at, hour12), centers):
            self.text(slot, y, digit, size, face=face, align="middle", ink=ink)

    def colon(self, x, y, gap, size, *, ink=BLACK):
        for top in (y, y + gap):
            self.box(x, top, x + size - 1, top + size - 1, ink=ink)

    def battery(self, x, y, *, ink=BLACK, gap=3, bar_width=9, bar_height=8):
        for slot in range(5):
            left = x + slot * (bar_width + gap)
            self.box(left, y, left + bar_width - 1, y + bar_height - 1, ink=ink,
                     width=0 if slot < (BATTERY + 10) // 20 else 1)


def date_short(at):
    return at.strftime("%a %d %b").upper()


def time_digits(at, hour12):
    return at.strftime("%I%M" if hour12 else "%H%M")


def time_mode(at, hour12):
    return at.strftime("%p") if hour12 else "24H"



def alarm_label(hour12):
    return f"ALARM {ALARM} AM" if hour12 else f"ALARM {ALARM}"


def steps_today(at):
    return "0" if at.hour == at.minute == 0 else STEPS


def signal_box(at, hour12):
    face = Face()
    face.box(7, 8, 192, 191, width=2)
    face.box(10, 11, 189, 35)
    face.text(17, 19, date_short(at), 13, face="mono", ink=WHITE)
    face.text(181, 19, at.strftime("%Y"), 13, face="mono", align="right", ink=WHITE)
    face.text(16, 44, "LOCAL TIME", 11, face="mono")
    face.rule(88, 48, 154, 48, width=2)
    face.text(183, 44, time_mode(at, hour12), 11, face="mono", align="right")
    starts = (15, 54, 110, 149)
    for x in starts:
        face.box(x, 57, x + 35, 121)
    face.digits(at, 68, 52, (32, 71, 127, 166), hour12=hour12, ink=WHITE)
    for x in starts:
        face.rule(x, 89, x + 35, 89, ink=WHITE)
    face.colon(96, 79, 15, 5)
    face.rule(15, 132, 184, 132, width=2)
    face.rule(102, 139, 102, 165, width=2)
    face.text(16, 140, "BATTERY", 11, face="mono")
    face.text(16, 154, f"{BATTERY}%", 18, face="mono")
    face.text(112, 140, "STEPS", 11, face="mono")
    face.text(112, 154, steps_today(at), 18, face="mono")
    face.rule(15, 174, 184, 174)
    face.menu(16, 179)
    face.text(184, 179, alarm_label(hour12), 11, face="mono", align="right")
    return face.image


def patchbay_divider(face, option):
    if option == "a":
        face.draw.rounded_rectangle((84, 126, 105, 166), radius=4, outline=WHITE)
        for y in range(132, 163, 6):
            face.rule(90, y + 2, 99, y, ink=WHITE, width=2)
    elif option == "b":
        face.draw.line([(95, 126), (95, 131), (87, 131), (87, 139), (103, 139), (103, 147),
                        (87, 147), (87, 155), (103, 155), (103, 163), (95, 163), (95, 168)],
                       fill=WHITE, width=1)
        for y in (126, 168):
            face.box(93, y - 2, 97, y + 2, ink=WHITE)
    elif option == "c":
        face.rule(95, 123, 95, 169, ink=WHITE)
        for y, radius in zip(range(127, 168, 4), (9, 3, 3, 3, 3, 9, 3, 3, 3, 3, 9)):
            face.rule(95 - radius, y, 95 + radius, y, ink=WHITE)
    elif option == "d":
        face.rule(89, 126, 89, 166, ink=WHITE)
        face.rule(101, 126, 101, 166, ink=WHITE)
        for y in (129, 163):
            face.box(86, y - 2, 104, y + 2, ink=WHITE)
            for x in (89, 101):
                face.box(x, y, x + 1, y + 1)
        for y in (138, 146, 154):
            face.rule(89, y, 101, y, ink=WHITE, width=2)
    elif option == "e":
        face.rule(95, 124, 95, 168, ink=WHITE)
        face.draw.ellipse((84, 135, 106, 157), fill=BLACK, outline=WHITE, width=2)
        face.draw.ellipse((89, 140, 101, 152), outline=WHITE, width=1)
        face.box(94, 145, 96, 147, ink=WHITE)
    else:
        raise ValueError(f"Unknown Patchbay divider: {option}")


def patchbay(at, hour12, *, divider="a"):
    face = Face()
    face.box(4, 4, 195, 195)
    face.box(8, 8, 191, 191, ink=WHITE, width=1)
    for x in (14, 185):
        for y in (14, 185):
            face.box(x-2, y-2, x+2, y+2, ink=WHITE)
            face.rule(x-2, y, x+2, y)
    face.text(20, 21, f"LOCAL {time_mode(at, hour12)}", 12, face="mono", ink=WHITE)
    face.text(179, 21, date_short(at), 13, face="mono", align="right", ink=WHITE)
    face.box(12, 38, 187, 106, ink=WHITE)
    face.box(15, 41, 184, 103, width=2)
    face.digits(at, 50, 59, (37, 74, 126, 163), hour12=hour12)
    face.colon(97, 65, 15, 5)
    for x in range(16, 185, 8):
        face.rule(x, 111, x, 116 if x in (16, 96, 176) else 114, ink=WHITE)
    patchbay_divider(face, divider)
    face.text(18, 127, "BATTERY", 11, face="mono", ink=WHITE)
    face.battery(18, 143, ink=WHITE)
    face.text(18, 155, f"{BATTERY}%", 13, face="mono", ink=WHITE)
    face.text(110, 127, "STEPS TODAY", 11, face="mono", ink=WHITE)
    face.text(110, 145, steps_today(at), 23, face="mono", ink=WHITE)
    face.rule(16, 171, 183, 171, ink=WHITE)
    face.menu(23, 180, ink=WHITE)
    face.text(176, 180, alarm_label(hour12), 11, face="mono", align="right", ink=WHITE)
    return face.image


PIXEL_DIGITS = (
    ("01110", "11011", "11011", "11011", "11011", "11011", "01110"),
    ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    ("01110", "11011", "00011", "00110", "01100", "11000", "11111"),
    ("11110", "00011", "00011", "01110", "00011", "00011", "11110"),
    ("00011", "00111", "01111", "11011", "11111", "00011", "00011"),
    ("11111", "11000", "11000", "11110", "00011", "00011", "11110"),
    ("01110", "11000", "11000", "11110", "11011", "11011", "01110"),
    ("11111", "00011", "00011", "00110", "00110", "01100", "01100"),
    ("01110", "11011", "11011", "01110", "11011", "11011", "01110"),
    ("01110", "11011", "11011", "01111", "00011", "00011", "01110"),
)


def pixel_digits(face, at, hour12):
    scale = 6
    for digit, x in zip(time_digits(at, hour12), (24, 61, 109, 146)):
        for row, line in enumerate(PIXEL_DIGITS[int(digit)]):
            for col, bit in enumerate(line):
                if bit == "1":
                    start_x, start_y = x + col * scale, 66 + row * scale
                    face.box(start_x, start_y, start_x + scale - 1, start_y + scale - 1)


def pocket_console(at, hour12):
    face = Face()
    face.draw.polygon([(15, 7), (181, 7), (181, 12), (192, 12), (192, 179),
                       (186, 179), (186, 191), (13, 191), (13, 185), (7, 185),
                       (7, 17), (15, 17)], fill=BLACK)
    face.box(12, 21, 187, 183, ink=WHITE)
    panel_stroke = 2
    for rect in ((13, 23, 186, 181), (19, 29, 180, 49), (19, 54, 180, 119),
                 (19, 124, 97, 154), (102, 124, 180, 154)):
        face.box(*rect, width=panel_stroke)
    face.text(25, 35, date_short(at), 12, face="mono")
    face.text(126, 35, time_mode(at, hour12), 11, face="mono", align="middle")
    face.battery(146, 35, gap=2, bar_width=4)
    pixel_digits(face, at, hour12)
    face.colon(97, 76, 14, 6)
    face.text(25, 130, "CHARGE", 10, face="mono")
    face.text(25, 141, f"{BATTERY}%", 15, face="mono")
    face.text(108, 130, "STEPS", 10, face="mono")
    face.text(108, 141, steps_today(at), 15, face="mono")
    face.menu(20, 166)
    face.text(179, 166, alarm_label(hour12), 11, face="mono", align="right")
    return face.image


def filing_card(at, hour12):
    face = Face()
    face.box(19, 23, 195, 185, width=1)
    face.draw.polygon([(8, 26), (112, 26), (112, 14), (185, 14), (185, 178), (8, 178)],
                      fill=WHITE, outline=BLACK, width=2)
    face.box(113, 15, 184, 37)
    face.text(149, 21, at.strftime("%d %b").upper(), 13, face="mono", align="middle", ink=WHITE)
    for y in range(38, 168, 14):
        face.draw.ellipse((13, y, 18, y+5), outline=BLACK)
    face.rule(22, 29, 22, 176)
    face.text(32, 36, "LOCAL TIME", 12, face="mono")
    face.text(32, 47, time_mode(at, hour12), 10, face="mono")
    face.text(173, 44, at.strftime("%A").upper(), 12, face="mono", align="right")
    face.rule(29, 59, 178, 59, width=2)
    face.digits(at, 71, 55, (45, 80, 128, 163), hour12=hour12)
    face.colon(101, 84, 15, 4)
    face.rule(29, 120, 178, 120, width=2)
    for x in range(32, 178, 7):
        face.rule(x, 147, x + 3, 147)
    face.text(32, 132, "BATTERY", 11, face="mono")
    face.text(173, 130, f"{BATTERY}%", 15, face="mono", align="right")
    face.text(32, 155, "STEPS", 11, face="mono")
    face.text(173, 154, steps_today(at), 15, face="mono", align="right")
    face.menu(16, 189)
    face.text(188, 189, alarm_label(hour12), 11, face="mono", align="right")
    return face.image


def chart_contours(face):
    for ring in range(5):
        points = []
        for sample in range(71):
            phi = sample / 70 * 2 * pi
            wave = 1 + .13 * sin(phi * 3 + .25) + .07 * cos(phi * 5 + .4)
            x = 154 + (13 + 5 * ring) * cos(phi) * wave
            y = 25 + (5 + 3.1 * ring) * sin(phi) * wave
            points.append((round(x), round(y)))
        face.draw.line(points, fill=BLACK)


def chart_table(at, hour12):
    face = Face()
    face.box(7, 7, 192, 192, width=1)
    for x in range(12, 187, 8):
        face.rule(x, 7, x, 12 if (x - 12) % 24 == 0 else 10)
        face.rule(x, 192, x, 187 if (x - 12) % 24 == 0 else 189)
    for y in range(16, 188, 8):
        face.rule(7, y, 12 if (y - 16) % 24 == 0 else 10, y)
        face.rule(192, y, 187 if (y - 16) % 24 == 0 else 189, y)
    chart_contours(face)
    face.text(17, 20, at.strftime("%a").upper(), 24, face="clock")
    face.box(83, 20, 112, 35, width=1)
    face.text(97, 24, time_mode(at, hour12), 11, face="mono", align="middle")
    face.text(18, 41, at.strftime("%d %b %Y").upper(), 11, face="mono")
    face.box(17, 56, 182, 126, width=2)
    for left in (19, 175):
        for top in (58, 119):
            face.box(left, top, left+4, top+4)
    face.digits(at, 68, 64, (38, 76, 125, 162), hour12=hour12)
    face.colon(97, 81, 18, 5)
    face.rule(17, 133, 182, 133)
    face.rule(99, 134, 99, 165)
    for x in range(151, 182, 4):
        face.rule(x, 136, min(x + 6, 182), 142)
    face.text(18, 140, "BATTERY", 11, face="mono")
    face.text(18, 154, f"{BATTERY}%", 16, face="mono")
    face.battery(53, 156, gap=1, bar_width=7)
    face.text(109, 140, "STEPS", 11, face="mono")
    face.text(109, 153, steps_today(at), 16, face="mono")
    face.rule(17, 172, 182, 172)
    face.menu(18, 179)
    face.text(182, 179, alarm_label(hour12), 10, face="mono", align="right")
    return face.image


FACES = {
    "signal-box": signal_box,
    "patchbay": patchbay,
    "pocket-console": pocket_console,
    "filing-card": filing_card,
    "chart-table": chart_table,
}
DATES = {
    "main": datetime(2026, 11, 19, 22, 28),
    "wide": datetime(2026, 11, 19, 8, 8),
    "late": datetime(2026, 11, 19, 23, 59),
    "midnight": datetime(2026, 11, 20),
}


def main():
    NATIVE.mkdir(parents=True, exist_ok=True)
    for slug, render in FACES.items():
        for variant, at in DATES.items():
            for hour12 in (False, True):
                art = render(at, hour12)
                assert art.mode == "1" and art.size == (200, 200)
                suffix = "-12h" if hour12 else ""
                target = NATIVE / f"{slug}{suffix}.png" if variant == "main" else WORK / f"{slug}-{variant}{suffix}.png"
                art.save(target, optimize=True)
    for option in "abcde":
        patchbay(DATES["main"], False, divider=option).save(
            NATIVE / f"patchbay-divider-{option}.png", optimize=True
        )


if __name__ == "__main__":
    main()
