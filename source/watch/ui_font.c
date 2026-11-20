#include "ui_font.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "frame.h"

typedef struct {
    uint16_t offset, advance;
    uint8_t width, height, top;
    int8_t left;
} ui_glyph_t;

typedef struct {
    const ui_glyph_t *glyphs;
    const uint8_t *pixels;
    const char *codes;
} ui_bitmap_font_t;

typedef struct {
    int left, right;
    unsigned top;
} ui_bounds_t;

#include "ui_font_data.inc"

static const ui_glyph_t *next_glyph(const ui_bitmap_font_t *font, const char **text)
{
    const unsigned char *p = (const unsigned char *)*text;
    unsigned code = *p++;
    if (code == 0xc2 && *p == 0xb7) {
        p++;
        code = 127; // Middle dot.
    } else if (code == 0xe2 && p[0] == 0x80 && (p[1] == 0x93 || p[1] == 0x99)) {
        code = p[1] == 0x93 ? 128 : 129; // En dash or curly apostrophe.
        p += 2;
    } else if (code == 0xe2 && p[0] == 0x86 && (p[1] == 0x91 || p[1] == 0x93)) {
        code = p[1] == 0x91 ? 130 : 131; // Up or down arrow.
        p += 2;
    } else if (code < 32 || code > 126) {
        code = '?';
    }
    *text = (const char *)p;
    if (font->codes) {
        const char *entry = strchr(font->codes, (int)code);
        return font->glyphs + (entry ? entry - font->codes : 0);
    }
    return font->glyphs + code - 32;
}

static ui_bounds_t text_bounds(const ui_bitmap_font_t *font, const char *text)
{
    ui_bounds_t bounds = {INT_MAX, INT_MIN, UINT_MAX};
    int cursor = 0;
    while (*text && cursor < INT_MAX - UINT16_MAX) {
        const ui_glyph_t *glyph = next_glyph(font, &text);
        if (glyph->width) {
            int left = (cursor + 32) / 64 + glyph->left;
            int right = left + glyph->width;
            if (left < bounds.left) bounds.left = left;
            if (right > bounds.right) bounds.right = right;
            if (glyph->top < bounds.top) bounds.top = glyph->top;
        }
        cursor += glyph->advance;
    }
    return bounds.top == UINT_MAX ? (ui_bounds_t){0} : bounds;
}

void ui_bitmap_ink(uint8_t *frame, const uint8_t *bitmap, int64_t x, int64_t y,
                   unsigned width, unsigned height, bool white)
{
    if (!width || !height || width > WATCH_WIDTH || height > WATCH_HEIGHT ||
        x >= WATCH_WIDTH || y >= WATCH_HEIGHT || x <= -(int)width || y <= -(int)height) {
        return;
    }
    int shift = ((int)x % 8 + 8) % 8;
    int column = ((int)x - shift) / 8;
    int first = y < 0 ? (int)-y : 0;
    int end = y + height > WATCH_HEIGHT ? WATCH_HEIGHT - (int)y : (int)height;
    unsigned stride = (width + 7) / 8;
    const uint8_t *source = bitmap + first * stride;
    for (int row = first; row < end; row++) {
        uint8_t *target = frame + ((int)y + row) * WATCH_STRIDE;
        uint8_t carry = 0;
        int col = column;
        for (unsigned i = 0; i < stride; i++, col++) {
            uint8_t ink = *source++;
            if ((unsigned)col < WATCH_STRIDE) {
                uint8_t mask = (ink >> shift) | carry;
                target[col] = white ? target[col] | mask : target[col] & (uint8_t)~mask;
            }
            carry = (uint8_t)(ink << (8 - shift));
        }
        if (carry && (unsigned)col < WATCH_STRIDE) {
            target[col] = white ? target[col] | carry : target[col] & (uint8_t)~carry;
        }
    }
}

int ui_text_width(ui_font_t font, const char *text)
{
    if ((unsigned)font >= UI_FONT_COUNT) {
        return 0;
    }
    ui_bounds_t bounds = text_bounds(&fonts[font], text);
    return bounds.right - bounds.left;
}

void ui_text_color(uint8_t *frame, ui_font_t font, int x, int y, const char *text, bool white)
{
    if ((unsigned)font >= UI_FONT_COUNT) {
        return;
    }
    const ui_bitmap_font_t *bitmap = &fonts[font];
    ui_bounds_t bounds = text_bounds(bitmap, text);
    int cursor = 0;
    while (*text && cursor < INT_MAX - UINT16_MAX) {
        const ui_glyph_t *glyph = next_glyph(bitmap, &text);
        int64_t left = (int64_t)x - bounds.left + (cursor + 32) / 64 + glyph->left;
        ui_bitmap_ink(frame, bitmap->pixels + glyph->offset, left,
                      (int64_t)y + glyph->top - bounds.top, glyph->width, glyph->height, white);
        cursor += glyph->advance;
    }
}

void ui_text(uint8_t *frame, ui_font_t font, int x, int y, const char *text)
{
    ui_text_color(frame, font, x, y, text, false);
}

void ui_clock_digit(uint8_t *frame, char digit, int center_x, int y, bool large)
{
    if (digit < '0' || digit > '9') {
        return;
    }
    const ui_glyph_t *glyph = (large ? clock_split_glyphs : clock_ledger_glyphs) + digit - '0';
    const uint8_t *pixels = large ? clock_split_pixels : clock_ledger_pixels;
    ui_bitmap_ink(frame, pixels + glyph->offset, (int64_t)center_x - glyph->width / 2, y,
                  glyph->width, glyph->height, false);
}
