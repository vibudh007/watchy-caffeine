#include "panel.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

enum { CS = 5, DC = 10, RESET = 9, BUSY = 19, CLOCK = 18, MOSI = 23 };

static bool transfer(spi_device_handle_t dev, bool data, const uint8_t *bytes, size_t length)
{
    spi_transaction_t transaction = {.length = length * 8, .tx_buffer = bytes};
    return gpio_set_level(DC, data) == ESP_OK && spi_device_polling_transmit(dev, &transaction) == ESP_OK;
}

static bool command(spi_device_handle_t dev, uint8_t opcode, const uint8_t *data, size_t length)
{
    return transfer(dev, false, &opcode, 1) && (!length || transfer(dev, true, data, length));
}

static bool wait_until_idle(int64_t end)
{
    while (gpio_get_level(BUSY)) {
        if (esp_timer_get_time() >= end) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

static bool wait_idle(unsigned max_ms)
{
    vTaskDelay(pdMS_TO_TICKS(2));
    return wait_until_idle(esp_timer_get_time() + max_ms * INT64_C(1000));
}

static void sleep_to_busy_release(int64_t end, panel_wait_t *wait)
{
    if (!gpio_get_level(BUSY) || gpio_wakeup_enable(BUSY, GPIO_INTR_LOW_LEVEL) != ESP_OK) {
        return;
    }
    if (esp_sleep_enable_gpio_wakeup() == ESP_OK) {
        int64_t remaining = end - esp_timer_get_time();
        if (remaining > 2000 && esp_sleep_enable_timer_wakeup((uint64_t)remaining) == ESP_OK) {
            if (gpio_get_level(BUSY)) {
                int64_t start = esp_timer_get_time();
                // GPIO wake and the enabled RTC bus workaround keep RTC_PERIPH on.
                if (esp_light_sleep_start() == ESP_OK) {
                    wait->sleep_call_us = (uint32_t)(esp_timer_get_time() - start);
                    wait->gpio_wake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
                }
            }
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        }
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    }
    gpio_wakeup_disable(BUSY);
    gpio_set_intr_type(BUSY, GPIO_INTR_DISABLE);
}

static bool range(spi_device_handle_t dev, int left, int top, int right, int bottom)
{
    const uint8_t xrange[] = {left, right};
    const uint8_t yrange[] = {top, 0, bottom, 0};
    return command(dev, 0x44, xrange, sizeof xrange) && command(dev, 0x45, yrange, sizeof yrange) &&
           command(dev, 0x4e, xrange, 1) && command(dev, 0x4f, yrange, 2);
}

static bool plane(spi_device_handle_t dev, uint8_t opcode, const uint8_t *frame,
                  int left, int top, int right, int bottom)
{
    if (!range(dev, left, top, right, bottom) || !command(dev, opcode, NULL, 0)) {
        return false;
    }
    if (left == 0 && right == WATCH_STRIDE - 1) {
        return transfer(dev, true, frame + top * WATCH_STRIDE, (bottom - top + 1) * WATCH_STRIDE);
    }
    for (int y = top; y <= bottom; y++) {
        if (!transfer(dev, true, frame + y * WATCH_STRIDE + left, right - left + 1)) {
            return false;
        }
    }
    return true;
}

static bool activate(spi_device_handle_t dev, uint8_t flag, unsigned timeout,
                     bool require_busy, bool allow_sleep, panel_wait_t *wait)
{
    if (!command(dev, 0x22, &flag, 1) || !command(dev, 0x20, NULL, 0)) {
        return false;
    }
    int64_t start = esp_timer_get_time();
    int64_t deadline_base = start;
    if (require_busy) {
        while (!gpio_get_level(BUSY)) {
            if (esp_timer_get_time() - start >= 30000) {
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else {
        // Retain the existing assertion delay for the optional power-off BUSY phase.
        vTaskDelay(pdMS_TO_TICKS(2));
        deadline_base = esp_timer_get_time();
    }
    int64_t end = deadline_base + timeout * INT64_C(1000);
    if (allow_sleep) {
        sleep_to_busy_release(end, wait);
    }
    bool ready = wait_until_idle(end);
    wait->elapsed_us = (uint32_t)(esp_timer_get_time() - start);
    return ready;
}

static bool refresh(spi_device_handle_t dev, const uint8_t *frame, const uint8_t *previous,
                    bool allow_sleep, panel_stats_t *stats)
{
    bool full = stats->full;
    int left = 0, right = WATCH_STRIDE - 1, top = 0, bottom = WATCH_HEIGHT - 1;
    if (!full) {
        left = WATCH_STRIDE;
        right = -1;
        top = WATCH_HEIGHT;
        bottom = -1;
        for (int y = 0; y < WATCH_HEIGHT; y++) {
            for (int x = 0; x < WATCH_STRIDE; x++) {
                int pos = y * WATCH_STRIDE + x;
                if (frame[pos] != previous[pos]) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                    if (y < top) top = y;
                    bottom = y;
                }
            }
        }
        if (bottom < 0) {
            return true;
        }
    }
    static const uint8_t output[] = {199, 0, 0};
    gpio_set_level(RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    bool ok = wait_idle(200) && command(dev, 0x12, NULL, 0) && wait_idle(200) &&
              command(dev, 0x01, output, sizeof output) && command(dev, 0x3c, (uint8_t[]){5}, 1) &&
              command(dev, 0x18, (uint8_t[]){0x80}, 1) && command(dev, 0x11, (uint8_t[]){3}, 1) &&
              plane(dev, 0x26, full ? frame : previous, left, top, right, bottom) &&
              plane(dev, 0x24, frame, left, top, right, bottom) &&
              activate(dev, full ? 0xf7 : 0xfc, full ? 4200 : 1500, true, allow_sleep, &stats->waveform);
    if (!full || !ok) {
        bool off = activate(dev, 0x83, 300, false, allow_sleep && ok, &stats->shutdown);
        ok = ok && off;
    }
    return command(dev, 0x10, (uint8_t[]){1}, 1) && ok;
}

bool panel_update(const uint8_t *frame, const uint8_t *previous, bool full,
                  bool allow_sleep, panel_stats_t *stats)
{
    int64_t start = esp_timer_get_time();
    *stats = (panel_stats_t){.full = full};
    gpio_config_t inputs = {.pin_bit_mask = UINT64_C(1) << BUSY, .mode = GPIO_MODE_INPUT};
    gpio_config_t outputs = {.pin_bit_mask = (UINT64_C(1) << DC) | (UINT64_C(1) << RESET), .mode = GPIO_MODE_OUTPUT};
    if (gpio_config(&inputs) != ESP_OK || gpio_set_level(RESET, 1) != ESP_OK || gpio_config(&outputs) != ESP_OK) {
        return false;
    }
    const spi_bus_config_t bus = {
        .sclk_io_num = CLOCK, .mosi_io_num = MOSI, .miso_io_num = -1,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = WATCH_FRAME_SIZE,
    };
    const spi_device_interface_config_t device = {.clock_speed_hz = 4000000, .spics_io_num = CS, .queue_size = 1};
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return false;
    }
    spi_device_handle_t dev;
    bool ok = spi_bus_add_device(SPI2_HOST, &device, &dev) == ESP_OK;
    if (ok) {
        ok = refresh(dev, frame, previous, allow_sleep, stats);
        spi_bus_remove_device(dev);
    }
    spi_bus_free(SPI2_HOST);
    gpio_set_level(RESET, 1);
    stats->update_us = (uint32_t)(esp_timer_get_time() - start);
    return ok;
}
