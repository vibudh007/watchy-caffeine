#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const gpio_num_t keys[] = {26, 25, 35, 4};
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t rtc, accel;
static bool recovered;
extern const uint8_t bma_start[] asm("_binary_bma423_config_start");
extern const uint8_t bma_end[] asm("_binary_bma423_config_end");

static void add_device(uint16_t address, i2c_master_dev_handle_t *device)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = address, .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, device) != ESP_OK) {
        *device = NULL;
    }
}

bool board_start(void)
{
    rtc_gpio_deinit(BOARD_RTC_INT);
    rtc_gpio_deinit(BOARD_MOTION_INT);
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        rtc_gpio_deinit(keys[i]);
    }
    gpio_config_t input = {.pin_bit_mask = board_wake_mask(0) | (UINT64_C(1) << BOARD_RTC_INT) |
                          (UINT64_C(1) << BOARD_MOTION_INT), .mode = GPIO_MODE_INPUT};
    if (gpio_config(&input) != ESP_OK) {
        return false;
    }
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = GPIO_NUM_21, .scl_io_num = GPIO_NUM_22,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
    };
    if (i2c_new_master_bus(&cfg, &bus) != ESP_OK) {
        return false;
    }
    recovered = false;
    add_device(0x51, &rtc);
    add_device(0x18, &accel);
    return rtc && accel;
}

void board_stop(void)
{
    if (rtc) {
        i2c_master_bus_rm_device(rtc);
        rtc = NULL;
    }
    if (accel) {
        i2c_master_bus_rm_device(accel);
        accel = NULL;
    }
    if (bus) {
        i2c_del_master_bus(bus);
        bus = NULL;
    }
}

uint8_t board_keys(void)
{
    uint8_t result = 0;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (gpio_get_level(keys[i])) {
            result |= 1u << i;
        }
    }
    return result;
}

uint64_t board_wake_mask(uint8_t held)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (!(held & (1u << i))) {
            mask |= UINT64_C(1) << keys[i];
        }
    }
    return mask;
}

static bool read_regs(i2c_master_dev_handle_t dev, uint8_t address, uint8_t *data, size_t size)
{
    if (!dev) {
        return false;
    }
    esp_err_t error = i2c_master_transmit_receive(dev, &address, 1, data, size, 20);
    if (error == ESP_ERR_TIMEOUT && !recovered) {
        recovered = true;
        if (i2c_master_bus_reset(bus) == ESP_OK) {
            error = i2c_master_transmit_receive(dev, &address, 1, data, size, 20);
        }
    }
    return error == ESP_OK;
}

static bool write_regs(i2c_master_dev_handle_t dev, uint8_t address, const uint8_t *data, size_t size)
{
    uint8_t packet[65];
    if (!dev || size > sizeof packet - 1) {
        return false;
    }
    packet[0] = address;
    memcpy(packet + 1, data, size);
    return i2c_master_transmit(dev, packet, size + 1, 20) == ESP_OK;
}

static bool reg(i2c_master_dev_handle_t dev, uint8_t address, uint8_t value)
{
    return write_regs(dev, address, &value, 1);
}

bool board_rtc_read(uint32_t *utc)
{
    uint8_t data[9];
    return read_regs(rtc, 0, data, sizeof data) && !(data[0] & 0x20) && watch_pcf_decode(data + 2, utc);
}

bool board_rtc_write(uint32_t utc)
{
    uint8_t data[7];
    if (!watch_pcf_encode(utc, data)) {
        return false;
    }
    bool stopped = reg(rtc, 0, 0x20);
    bool written = stopped && write_regs(rtc, 2, data, sizeof data);
    /* A timed-out STOP transaction may still have reached the clock. */
    bool running = reg(rtc, 0, 0) || reg(rtc, 0, 0);
    uint32_t check;
    return running && written && board_rtc_read(&check) && check >= utc && check - utc <= 1;
}

static bool rtc_quiet(uint8_t *quiet)
{
    uint8_t status;
    if (!read_regs(rtc, 1, &status, 1)) {
        return false;
    }
    /* A zero clears AF, a one preserves TF even if TF changed after the read. */
    *quiet = (status & 0x10) | 0x04;
    return !(status & 0x0b) || reg(rtc, 1, *quiet);
}

bool board_rtc_disarm(void)
{
    uint8_t quiet;
    return rtc_quiet(&quiet);
}

bool board_rtc_arm(uint32_t target)
{
    uint8_t quiet;
    if (!watch_utc_valid(target) || target % 60 || !rtc_quiet(&quiet)) {
        return false;
    }
    unsigned minute = target / 60 % 60, hour = target / 3600 % 24;
    // Alarm fields, CLKOUT off, then stopped timer at the lowest source rate.
    const uint8_t registers[] = {(minute / 10 << 4) | (minute % 10), (hour / 10 << 4) | (hour % 10), 0x80, 0x80, 0, 3};
    return write_regs(rtc, 9, registers, sizeof registers) && reg(rtc, 1, quiet | 2) &&
           gpio_get_level(BOARD_RTC_INT) != 0;
}

int16_t board_battery(void)
{
    adc_oneshot_unit_handle_t adc;
    const adc_oneshot_unit_init_cfg_t init = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&init, &adc) != ESP_OK) {
        return -1;
    }
    adc_cali_handle_t calibration;
    const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    const adc_cali_line_fitting_config_t cal = {
        .unit_id = ADC_UNIT_1, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
    };
    int sum = 0;
    int good = 0;
    if (adc_oneshot_config_channel(adc, ADC_CHANNEL_6, &channel) == ESP_OK &&
        adc_cali_create_scheme_line_fitting(&cal, &calibration) == ESP_OK) {
        for (int i = 0; i < 4; i++) {
            int raw, millivolts;
            if (adc_oneshot_read(adc, ADC_CHANNEL_6, &raw) == ESP_OK &&
                adc_cali_raw_to_voltage(calibration, raw, &millivolts) == ESP_OK) {
                sum += millivolts * 2;
                good++;
            }
        }
        adc_cali_delete_scheme_line_fitting(calibration);
    }
    adc_oneshot_del_unit(adc);
    if (!good) {
        return -1;
    }
    int mv = sum / good;
    if (mv < 3000 || mv > 4350) {
        return -1;
    }
    int percent = (mv - 3400) / 8;
    return percent < 0 ? 0 : percent > 100 ? 100 : (int16_t)(percent / 5 * 5);
}

static bool feature_address(unsigned word)
{
    const uint8_t pointer[] = {word & 15u, word >> 4};
    return write_regs(accel, 0x5b, pointer, sizeof pointer);
}

static bool upload_steps(void)
{
    if (bma_end - bma_start != 6144 || !reg(accel, 0x59, 0)) {
        return false;
    }
    for (unsigned index = 0; index < 6144; index += 32) {
        if (!feature_address(index / 2) || !write_regs(accel, 0x5e, bma_start + index, 32)) {
            return false;
        }
    }
    if (!reg(accel, 0x59, 1)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(155));
    return true;
}

static bool configure_image(uint16_t *base)
{
    uint8_t state;
    if (*base != BOARD_BMA_UNINITIALIZED && read_regs(accel, 0x2a, &state, 1) && (state & 15) == 1) {
        return true;
    }
    *base = BOARD_BMA_UNINITIALIZED;
    // A firmware reboot alone can leave a different feature image running on the sensor.
    if (!reg(accel, 0x7e, 0xb6)) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (!reg(accel, 0x7c, 0)) return false;
    esp_rom_delay_us(500);
    if (!upload_steps() || !read_regs(accel, 0x2a, &state, 1) || (state & 15) != 1) return false;
    uint8_t lo, hi;
    if (!read_regs(accel, 0x5b, &lo, 1) || !read_regs(accel, 0x5c, &hi, 1)) return false;
    *base = (uint16_t)((hi << 4) | (lo & 15));
    return true;
}

static bool enable_motion(bool steps, bool wake, uint16_t base)
{
    // Bosch 2020 image. Step control starts at byte 0x3a of the feature window.
    enum { STEP_OFFSET = 0x3a };
    uint8_t features[2];
    if ((unsigned)base + STEP_OFFSET / 2 + sizeof features / 2 > 4096 ||
        !feature_address(base + STEP_OFFSET / 2) || !read_regs(accel, 0x5e, features, sizeof features)) {
        return false;
    }
    features[0] = 0; /* Watermark remains zero. */
    features[1] = (features[1] & 0xc0) | (steps ? 0x10 : 0);
    // Any-motion threshold is 1/16 g. Qualify for three 50 Hz samples on any axis.
    const uint8_t any_motion[] = {0x80, 0, 3, wake ? 0xe0 : 0};
    const uint8_t sampling[2] = {0x37, 0};
    return feature_address(base + STEP_OFFSET / 2) && write_regs(accel, 0x5e, features, sizeof features) &&
           feature_address(base) && write_regs(accel, 0x5e, any_motion, sizeof any_motion) && feature_address(base) &&
           write_regs(accel, 0x40, sampling, sizeof sampling) && reg(accel, 0x7d, 4);
}

bool board_motion(bool steps, bool wake, uint16_t *feature_base)
{
    uint8_t id;
    if (!read_regs(accel, 0, &id, 1) || id != 0x13 || !reg(accel, 0x7c, 0)) {
        return false;
    }
    esp_rom_delay_us(500);
    const uint8_t interrupts[] = {wake ? 0x0a : 0, 0, wake, wake ? 0x20 : 0, 0, 0};
    bool active = steps || wake;
    bool ok = (!active || configure_image(feature_base)) && write_regs(accel, 0x53, interrupts, sizeof interrupts) &&
              (active ? enable_motion(steps, wake, *feature_base) : reg(accel, 0x7d, 0));
    if (!ok) {
        reg(accel, 0x7d, 0);
    }
    bool aps = reg(accel, 0x7c, 1);
    esp_rom_delay_us(500);
    if (!ok || !aps) *feature_base = BOARD_BMA_UNINITIALIZED;
    return aps && ok;
}

bool board_motion_ack(void)
{
    uint8_t status;
    return read_regs(accel, 0x1c, &status, 1) && (status & 0x20) != 0;
}

bool board_step_sample(uint32_t *count)
{
    uint8_t status, value[4];
    if (!read_regs(accel, 0x2a, &status, 1) || (status & 15) != 1 ||
        !read_regs(accel, 0x1e, value, sizeof value)) {
        return false;
    }
    *count = (uint32_t)value[0] | (uint32_t)value[1] << 8 | (uint32_t)value[2] << 16 | (uint32_t)value[3] << 24;
    return true;
}
