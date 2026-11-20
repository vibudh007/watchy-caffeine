"""Generate fixed one-bit artwork for the experimental watchfaces."""

from math import ceil, cos, floor, radians, sin, sqrt
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "assets" / "fonts"
FONTS = ROOT / "assets" / "fonts"
OUTPUT = ROOT / "source" / "watch" / "face_exp_assets.inc"
ONES = ("ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE",
        "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN",
        "SEVENTEEN", "EIGHTEEN", "NINETEEN")
HOURS = (*ONES, "TWENTY", "TWENTY-ONE", "TWENTY-TWO", "TWENTY-THREE")
MINUTES = ("O’CLOCK", *ONES[1:], "TWENTY", "THIRTY", "FORTY", "FIFTY", "OH")


def word_mask(word, size, weight, max_width):
    """Return the tight black-ink mask at the largest fitting point size."""
    font = ImageFont.truetype(str(ART / "Fraunces.ttf"), size)
    font.set_variation_by_axes([size, weight, 18, 1])
    box = font.getbbox(word)
    while box[2] - box[0] > max_width:
        size -= 1
        font = ImageFont.truetype(str(ART / "Fraunces.ttf"), size)
        font.set_variation_by_axes([size, weight, 18, 1])
        box = font.getbbox(word)
    image = Image.new("1", (box[2] - box[0], box[3] - box[1]), 0)
    ImageDraw.Draw(image).text((-box[0], -box[1]), word, font=font, fill=1)
    return image.crop(image.getbbox())


def hour_digit(digit):
    """Stretch each Barlow digit vertically within a fixed 37-pixel column."""
    font = ImageFont.truetype(str(FONTS / "BarlowSemiCondensed-Bold.ttf"), 99)
    x0, y0, x1, y1 = font.getbbox(digit)
    original = Image.new("1", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(original).text((-x0, -y0), digit, font=font, fill=1)
    mask = original.crop(original.getbbox())
    width = max(1, round(mask.width * 34 / 45))
    return mask.resize((width, 121), Image.Resampling.NEAREST)


def quarter_rim():
    """Draw the perimeter, ticks and four quadrant divisions once."""
    image = Image.new("1", (112, 110), 0)
    draw = ImageDraw.Draw(image)
    x, y, radius = 56, 55, 47
    draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline=1, width=2)
    draw.line((x, y - radius, x, y + radius), fill=1)
    draw.line((x - radius, y, x + radius, y), fill=1)
    for minute in range(60):
        angle = radians(-90 + minute * 6)
        inner, outer = radius + 3, radius + (7 if minute % 5 == 0 else 4)
        draw.line((x + round(inner * cos(angle)), y + round(inner * sin(angle)),
                   x + round(outer * cos(angle)), y + round(outer * sin(angle))), fill=1)
    return image


def atlas_grid():
    """Draw the stationary grid without clock labels or indicator ink."""
    image = Image.new("1", (168, 120), 0)
    draw = ImageDraw.Draw(image)
    columns = tuple(6 + 14 * index for index in range(12))
    rows = tuple(2 + 19 * index for index in range(7))
    for x in columns:
        draw.line((x, rows[0], x, rows[-1]), fill=1)
    for y in rows:
        draw.line((1, y, 165, y), fill=1)
        for x in columns:
            draw.ellipse((x - 2, y - 2, x + 2, y + 2), fill=0, outline=1)
    return image


def layout(image):
    """Pack a cropped full-width mask into a width, height and stroke buffer."""
    width, height = image.size
    assert width <= 255 and height <= 200
    bits = image.tobytes()
    assert len(bits) == ((width + 7) // 8) * height
    return width, height, bits


def byte_rows(data, *, unsigned=True):
    """Format a predictable number of numeric literals per C source line."""
    format_byte = (lambda byte: f"0x{byte:02x}") if unsigned else str
    return ["    " + ", ".join(format_byte(byte) for byte in data[i:i + 16]) + ","
            for i in range(0, len(data), 16)]


def bitmap_table(name, images):
    """Place bitmap descriptors in one table and pixel bytes in one flash array."""
    descriptors, data = [], bytearray()
    for image in images:
        width, height, bits = layout(image)
        descriptors.append(f"    {{{len(data)}, {width}, {height}}},")
        data.extend(bits)
    assert len(data) <= 65535
    return [
        f"static const exp_mask_t {name}_masks[] = {{", *descriptors, "};",
        f"static const uint8_t {name}_pixels[] = {{", *byte_rows(data), "};",
    ]


def geometry_tables():
    """Emit integer coordinates and solder widths derived from the proof geometry."""
    rows = [ceil(5.7 * sqrt(i / 10)) for i in range(11)]
    lobe_masks = []
    for mass, reach in enumerate(rows):
        radius_squared = 5.7 * 5.7 * mass / 10
        entries = []
        for dy in range(-6, 7):
            remaining = radius_squared - dy * dy
            width = 2 * floor(sqrt(remaining)) + 1 if mass and abs(dy) <= reach and remaining >= 0 else 0
            entries.append(width)
        lobe_masks.append(entries)
    solder = []
    for minute in range(1, 10):
        fraction = minute / 10
        upper, lower = 5.7 * sqrt(1 - fraction), 5.7 * sqrt(fraction)
        throat = fraction * 19
        neck = 0.45 + 0.9 * sin(radians(180 * fraction))
        diameters = []
        for y in range(20):
            if y <= throat:
                blend, radius = (throat - y) / throat, upper
            else:
                blend, radius = (y - throat) / (19 - throat), lower
            diameters.append(2 * round(neck + (radius - neck) * blend ** 3) + 1)
        solder.append(diameters)
    yield "static const uint8_t atlas_lobes[11][13] = {"
    for row in lobe_masks:
        yield "    {" + ", ".join(map(str, row)) + "},"
    yield "};"
    yield "static const uint8_t atlas_necks[9][20] = {"
    for row in solder:
        yield "    {" + ", ".join(map(str, row)) + "},"
    yield "};"
    yield "static const int8_t quarter_minute[15][2] = {"
    for offset in range(15):
        angle = radians(offset * 6)
        yield f"    {{{round(40 * sin(angle))}, {-round(40 * cos(angle))}}},"
    yield "};"
    yield "static const uint8_t quarter_width[48] = {"
    yield from byte_rows([floor(sqrt(47 * 47 - dy * dy)) for dy in range(48)], unsigned=False)
    yield "};"


def main():
    chunks = ["/* Generated by tools/make_watchy_experimental_assets.py. */", ""]
    for name, images in (
        ("exp_hours", (word_mask(word, 68, 775, 174) for word in HOURS)),
        ("exp_minutes", (word_mask(word, 39, 660, 173) for word in MINUTES)),
        ("exp_digits", (hour_digit(digit) for digit in "0123456789")),
        ("exp_quarter", (quarter_rim(),)),
        ("exp_atlas", (atlas_grid(),)),
    ):
        chunks.extend(bitmap_table(name, images))
        chunks.append("")
    chunks.extend(geometry_tables())
    OUTPUT.write_text("\n".join(chunks) + "\n")


if __name__ == "__main__":
    main()
