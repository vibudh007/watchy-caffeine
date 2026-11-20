#include "demo.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "board.h"

#define REBOOT_PENDING UINT32_C(0x52444331)
#define REBOOT_DONE UINT32_C(0x52444332)

static RTC_NOINIT_ATTR struct {
    uint32_t tag;
    uint32_t boot_utc;
    int16_t offset;
    uint8_t external;
    uint8_t reports;
} reboot;

bool demo_reboot_begin(int16_t *offset)
{
    if (esp_reset_reason() != ESP_RST_SW || reboot.tag != REBOOT_PENDING ||
        reboot.offset < -720 || reboot.offset > 840 || reboot.offset % 15) {
        return false;
    }
    *offset = reboot.offset;
    reboot.tag = 0;
    return true;
}

void demo_reboot_finish(uint32_t utc, bool external)
{
    reboot.boot_utc = utc;
    reboot.external = external;
    reboot.reports = 12;
    reboot.tag = REBOOT_DONE;
}

void demo_reboot_report(void)
{
    if (reboot.tag == REBOOT_DONE && reboot.reports && reboot.reports <= 12 && reboot.external <= 1) {
        printf("WATCHY_DEMO_REBOOT boot_utc=%" PRIu32 " external=%u\n", reboot.boot_utc, reboot.external);
        reboot.reports--;
    }
}

void demo_restart_if_held(uint8_t buttons, int16_t offset)
{
    const uint8_t combo = (1u << KEY_MENU) | (1u << KEY_BACK);
    if ((buttons & combo) != combo) {
        return;
    }
    for (unsigned check = 0; check < 30; check++) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if ((board_keys() & combo) != combo) {
            return;
        }
    }
    reboot.offset = offset;
    reboot.tag = REBOOT_PENDING;
    esp_restart();
}

bool demo_seed(uint32_t *utc, int16_t *offset, bool *portal)
{
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) != ESP_OK) {
        return false;
    }
    puts("WATCHY_DEMO_READY");
    fflush(stdout);
    char line[48];
    size_t size = 0;
    bool complete = false;
    int64_t timeout = esp_timer_get_time() + 2500000;
    while (size < sizeof line - 1 && esp_timer_get_time() < timeout) {
        char byte;
        if (uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(50)) != 1) {
            continue;
        }
        if (byte == '\n') {
            complete = true;
            break;
        }
        line[size++] = byte;
    }
    uart_driver_delete(UART_NUM_0);
    line[size] = 0;
    uint64_t seconds;
    int zone;
    char excess;
    const char *arguments = line;
    bool start_portal = !strncmp(line, "DEMO_PORTAL ", 12);
    if (start_portal) {
        arguments += 12;
    } else if (!strncmp(line, "DEMO_CLOCK ", 11)) {
        arguments += 11;
    } else {
        return false;
    }
    if (!complete || sscanf(arguments, "%10" SCNu64 " %5d%c", &seconds, &zone, &excess) != 2 ||
        seconds > WATCH_LAST_UTC ||
        !watch_utc_valid((uint32_t)seconds) || zone < -720 || zone > 840 || zone % 15) {
        return false;
    }
    *utc = (uint32_t)seconds;
    *offset = (int16_t)zone;
    *portal = start_portal;
    return true;
}

void demo_emit(const uint8_t pixels[WATCH_FRAME_SIZE])
{
    static const char hex[] = "0123456789abcdef";
    char chunk[128];
    fputs("WATCHY_DEMO_FRAME ", stdout);
    for (size_t start = 0; start < WATCH_FRAME_SIZE; start += sizeof chunk / 2) {
        size_t count = WATCH_FRAME_SIZE - start;
        if (count > sizeof chunk / 2) {
            count = sizeof chunk / 2;
        }
        for (size_t i = 0; i < count; i++) {
            chunk[i * 2] = hex[pixels[start + i] >> 4];
            chunk[i * 2 + 1] = hex[pixels[start + i] & 15];
        }
        fwrite(chunk, 2, count, stdout);
    }
    putchar('\n');
    fflush(stdout);
}
