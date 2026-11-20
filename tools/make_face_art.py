"""Generate one-bit artwork and optional ink masks for the five art faces."""

import argparse
from math import atan2, ceil, cos, degrees, pi, sin, sqrt
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / "assets/fonts"
ART_DIR = ROOT / "assets/fonts"
OUT = ROOT / "source/watch/face_art_data.inc"


def rounded_points(points):
    return [(round(x), round(y)) for x, y in points]


def cubic(a, b, c, d, count=24):
    return [(a[0]*(1-t)**3 + 3*b[0]*t*(1-t)**2 + 3*c[0]*(1-t)*t*t + d[0]*t**3,
             a[1]*(1-t)**3 + 3*b[1]*t*(1-t)**2 + 3*c[1]*(1-t)*t*t + d[1]*t**3)
            for t in (n/count for n in range(count+1))]


def leaf(draw, root, tip, width):
    dx, dy = tip[0]-root[0], tip[1]-root[1]
    length = sqrt(dx*dx+dy*dy)
    nx, ny = -dy*width/length, dx*width/length
    upper = cubic(root, (root[0]+dx*.28+nx, root[1]+dy*.28+ny),
                  (root[0]+dx*.69+nx, root[1]+dy*.69+ny), tip)
    lower = cubic(tip, (root[0]+dx*.63-nx, root[1]+dy*.63-ny),
                  (root[0]+dx*.28-nx*.5, root[1]+dy*.28-ny*.5), root)
    draw.polygon(rounded_points(upper+lower), fill=0)
    if length > 20:
        draw.line((*rounded_points([(root[0]+dx*.28,root[1]+dy*.28)])[0],
                  *rounded_points([(root[0]+dx*.76,root[1]+dy*.76)])[0]), fill=255, width=1)


def woodcut_tide():
    image = Image.new("1", (200, 200), 255)
    draw = ImageDraw.Draw(image)
    # Distant sandbar and horizon, with a low, cross-hatched sun.
    draw.ellipse((140, 22, 168, 50), fill=0)
    for y in (35, 40, 45):
        draw.line((139, y, 169, y), fill=255, width=1)
    silhouette = [(0,48),(18,46),(34,47),(46,40),(61,38),(80,45),(102,46),(121,49),
                  (143,47),(177,47),(199,46),(199,51),(0,51)]
    draw.polygon(silhouette, fill=0)
    draw.line((0,54,199,54), fill=0)
    # The sea stays below this time panel. Its moving-looking curves are fixed ink.
    crest = cubic((0,157),(48,168),(46,120),(96,139))
    crest += cubic((96,139),(130,152),(144,174),(199,145))
    draw.polygon(rounded_points(crest)+[(199,199),(0,199)], fill=0)
    for offset, width in ((7,2),(13,1),(20,2),(29,1),(39,2),(50,1),(62,2)):
        draw.line(rounded_points([(x,y+offset+2*sin(x/15)) for x,y in crest]), fill=255, width=width)
    return image


def concrete_study():
    image = Image.new("1", (200, 200), 255)
    draw = ImageDraw.Draw(image)
    # Each geometric solid is deliberately independent of the changing clock digits.
    draw.rectangle((112,59,199,137), fill=0)
    draw.polygon([(125,45),(191,16),(199,26),(199,56)], fill=0)
    draw.polygon([(0,139),(71,139),(53,199),(0,199)], fill=0)
    draw.ellipse((15,22,44,51), fill=0)
    draw.line((58,35,96,35), fill=0, width=2)
    draw.line((57,36,57,52), fill=0, width=2)
    draw.line((111,146,191,146), fill=0, width=2)
    return image


def blockprint_iris():
    image = Image.new("1", (200, 200), 255)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((4,4,195,195), radius=11, outline=0, width=2)
    draw.rounded_rectangle((8,8,191,191), radius=9, outline=0, width=1)
    for mirrored in (False, True):
        def point(x, y):
            return (200-x if mirrored else x, y)
        for top in (True, False):
            def oriented(x, y):
                return point(x, y if top else 200-y)
            stem = cubic(oriented(20,69),oriented(16,31),oriented(48,16),oriented(75,22))
            draw.line(rounded_points(stem), fill=0, width=2)
            for tail, tip, width in [((24,46),(18,27),5),((29,35),(44,25),5),((36,27),(29,15),4),
                                     ((45,23),(64,32),5),((57,21),(61,13),4),((68,22),(77,29),3)]:
                leaf(draw, oriented(*tail), oriented(*tip), width)
        for y in (80,100,120):
            x = point(15,y)[0]
            draw.polygon([(x,y-4),(x+2,y),(x,y+4),(x-2,y)],fill=0)
    # A broad empty cartouche keeps the optical clock larger than the ornament.
    for sign in (1,-1):
        petal_root = (100,40 if sign>0 else 160)
        for dx,dy in ((19,-12), (12,-22), (5,-26), (0,-28),(-5,-26),(-12,-22),(-19,-12)):
            leaf(draw, petal_root, (100+dx,petal_root[1]+sign*dy), 3.4)
    draw.line((32,63,168,63), fill=0)
    draw.line((32,128,168,128), fill=0)
    return image


def ink_observatory():
    image = Image.new("1", (200, 200), 0)
    draw = ImageDraw.Draw(image)
    # Two engraved orbit tracks are graphic borders and carry no astronomical data.
    for radius, width in ((82,2),(88,1)):
        box = (100-radius,100-radius,100+radius,100+radius)
        draw.arc(box,204,336,fill=255,width=width)
        draw.arc(box,24,156,fill=255,width=width)
    for phase in (26,33,50,68,90,112,130,147,154,208,220,250,270,295,322,328):
        theta = phase*pi/180
        start = (round(100+76*cos(theta)),round(100+76*sin(theta)))
        end = (round(100+81*cos(theta)),round(100+81*sin(theta)))
        draw.line((*start,*end),fill=255)
    # Fixed stars of three sizes stay outside the clock and data fields.
    for x,y,r in ((27,42,4),(61,40,5),(148,40,7),(164,49,4),
                  (119,27,3),(135,153,4),(68,158,6),(103,174,3),(39,146,4)):
        draw.polygon([(x,y-r),(x+1,y-1),(x+r,y),(x+1,y+1),(x,y+r),(x-1,y+1),(x-r,y),(x-1,y-1)],fill=255)
    for x,y in ((20,53),(31,29),(35,56),(45,38),(54,23),(55,50),(71,20),(75,53),
                (81,30),(92,36),(99,19),(106,48),(114,39),(126,50),(133,18),
                (142,28),(157,21),(162,38),(173,32),(176,57),(184,52),(24,149),
                (26,168),(34,159),(44,175),(49,156),(55,170),(62,179),(76,170),
                (83,154),(84,181),(98,159),(110,153),(117,164),(129,173),(143,166),
                (149,152),(156,162),(161,148),(170,159),(179,141),(185,151)):
        draw.point((x,y), fill=255)
    for x,y in ((39,50),(68,49),(92,24),(112,33),(153,54),(49,163),(91,168),(119,177),(145,175)):
        draw.rectangle((x,y,x+1,y+1), fill=255)
    draw.rectangle((33,49,55,71), fill=0)
    return image


def film_leader():
    image = Image.new("1", (200, 200), 255)
    draw = ImageDraw.Draw(image)
    for left in (0,181):
        draw.rectangle((left,0,left+18,199),fill=0)
        for y in range(9,191,22):
            draw.rounded_rectangle((left+5,y,left+13,y+12),radius=2,fill=255)
    # Consecutive exposures share dimensions; the screen crops the adjacent frames.
    for top in (-95, 24, 143):
        bottom = top + 112
        draw.rounded_rectangle((25,top,174,bottom), radius=7, outline=0, width=2)
        for x, y, dx, dy in ((30,top+6,9,9),(169,top+6,-9,9),
                             (30,bottom-6,9,-9),(169,bottom-6,-9,-9)):
            draw.line((x+dx,y,x,y,x,y+dy),fill=0,width=2)
    return image


def c_bytes(name, values):
    rows = (", ".join(f"0x{value:02x}" for value in values[i:i+16])
            for i in range(0, len(values), 16))
    return f"static const uint8_t {name}[] = {{\n    " + ",\n    ".join(rows) + ",\n};\n"


def digit_font(name, file, size, axes=()):
    font = ImageFont.truetype(file, size)
    if axes:
        font.set_variation_by_axes(list(axes))
    pixels, metrics = bytearray(), []
    for digit in "0123456789":
        left, top, right, bottom = font.getbbox(digit)
        image = Image.new("1", (right-left, bottom-top))
        ImageDraw.Draw(image).text((-left, -top), digit, font=font, fill=255)
        mask = image.crop(image.getbbox())
        metrics.append((len(pixels), *mask.size))
        pixels.extend(mask.tobytes())
    rows = "".join(f"    {{{offset}, {width}, {height}}},\n" for offset, width, height in metrics)
    return c_bytes(f"{name}_bits", pixels) + f"static const art_digit_t {name}[] = {{\n{rows}}};\n"


def ring_thresholds():
    """Clockwise angle is the minimum remaining charge to keep the bit white."""
    radius = 11
    for y in range(-radius, radius+1):
        for x in range(-radius, radius+1):
            if 36 < x*x + y*y <= radius*radius:
                yield max(1, ceil(degrees(atan2(x, -y)) % 360 * 100 / 360))
            else:
                yield 0xff


def bell_bitmap():
    image = Image.new("1", (15, 13))
    draw = ImageDraw.Draw(image)
    draw.arc((2, 0, 12, 10), 180, 360, fill=255)
    draw.line((2, 5, 2, 9, 0, 10, 14, 10, 12, 9, 12, 5), fill=255)
    draw.line((6, 12, 8, 12), fill=255)
    return image.tobytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    arrays = ["/* Generated by tools/make_face_art.py. The base artwork is original. */\n"]
    for shape in (woodcut_tide, concrete_study, blockprint_iris, ink_observatory, film_leader):
        arrays.append(c_bytes(shape.__name__ + "_base", shape().tobytes()))
    barlow = FONT_DIR / "BarlowSemiCondensed-Bold.ttf"
    arrays.extend((
        digit_font("barlow_61", barlow, 61),
        digit_font("barlow_68", barlow, 68),
        digit_font("fraunces_50", ART_DIR / "Fraunces.ttf", 50, (48, 710, 25, 0)),
        digit_font("geist_mono_47", FONT_DIR / "GeistMono.ttf", 47, (700,)),
        c_bytes("ring_charge", bytes(ring_thresholds())),
        c_bytes("alarm_ink", bell_bitmap()),
    ))
    output = "\n".join(arrays)
    if args.check:
        assert OUT.read_text() == output, OUT
    else:
        OUT.write_text(output)


if __name__ == "__main__":
    main()
