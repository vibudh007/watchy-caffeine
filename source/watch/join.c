#include "frame.h"

#include <stdio.h>
#include <string.h>

#include "qrcodegen.h"
#include "ui_font.h"

enum { QR_VERSION = 3, QR_MODULE_PX = 4, QR_TOP = 32 };

static void center(uint8_t *frame, ui_font_t font, int y, const char *text)
{
    ui_text(frame, font, (WATCH_WIDTH - ui_text_width(font, text)) / 2, y, text);
}

bool watch_join_frame(uint8_t frame[WATCH_FRAME_SIZE], const char *ssid, const char *password)
{
    char data[56];
    int length = snprintf(data, sizeof data, "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    if (length < 0 || (size_t)length >= sizeof data) {
        return false;
    }
    uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
    uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VERSION)];
    if (!qrcodegen_encodeText(data, scratch, qr, qrcodegen_Ecc_MEDIUM, QR_VERSION, QR_VERSION,
                              qrcodegen_Mask_AUTO, true)) {
        return false;
    }
    snprintf(data, sizeof data, "%s  %s", ssid, password);
    if (ui_text_width(UI_MONO_TINY, data) > WATCH_WIDTH - 8) {
        return false;
    }
    memset(frame, 0xff, WATCH_FRAME_SIZE);
    int size = qrcodegen_getSize(qr);
    int left = (WATCH_WIDTH - size * QR_MODULE_PX) / 2;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qr, x, y)) {
                watch_fill(frame, left + x * QR_MODULE_PX, QR_TOP + y * QR_MODULE_PX,
                           QR_MODULE_PX, QR_MODULE_PX);
            }
        }
    }
    ui_text(frame, UI_TINY, 8, 3, "Back Cancel");
    const char *scan = "Scan to join Wi-Fi";
    ui_text(frame, UI_TINY, WATCH_WIDTH - 8 - ui_text_width(UI_TINY, scan), 3, scan);
    center(frame, UI_MONO_SMALL, 164, "http://192.168.4.1");
    center(frame, UI_MONO_TINY, 179, data);
    return true;
}
