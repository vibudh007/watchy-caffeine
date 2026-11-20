#include "watch.h"

static bool leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int watch_month_days(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month >= 1 && month <= 12 ? days[month - 1] + (month == 2 && leap(year)) : 0;
}

bool watch_utc_valid(uint32_t utc)
{
    return utc >= WATCH_FIRST_UTC && utc <= WATCH_LAST_UTC;
}

bool watch_encode_date(const watch_date_t *date, int16_t offset_min, uint32_t *utc)
{
    if (date->year < 2000 || date->year > 2099 || date->day < 1 ||
        date->day > watch_month_days(date->year, date->month) ||
        date->hour < 0 || date->hour > 23 || date->minute < 0 || date->minute > 59 ||
        date->second < 0 || date->second > 59) {
        return false;
    }
    static const uint16_t month_start[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int years = date->year - 2000;
    int days = years * 365 + (years + 3) / 4 + month_start[date->month - 1] + date->day - 1;
    days += date->month > 2 && leap(date->year);
    int64_t result = WATCH_FIRST_UTC + (int64_t)days * 86400 + date->hour * 3600 +
                     (date->minute - offset_min) * 60 + date->second;
    if (result < WATCH_FIRST_UTC || result > WATCH_LAST_UTC) {
        return false;
    }
    *utc = (uint32_t)result;
    return true;
}

bool watch_decode_date(uint32_t utc, int16_t offset_min, watch_date_t *date)
{
    if (!watch_utc_valid(utc)) {
        return false;
    }
    int64_t shifted = (int64_t)utc - WATCH_FIRST_UTC + offset_min * 60;
    int days = (int)(shifted / 86400);
    int seconds = (int)(shifted % 86400);
    if (seconds < 0) {
        seconds += 86400;
        days--;
    }
    int year = 2000;
    if (days < 0) {
        // Even the full int16_t offset range can only reach December 1999.
        year--;
        days += 365;
    } else {
        // This domain starts on a leap year and does not cross a skipped leap day.
        int cycles = days / 1461;
        year += cycles * 4;
        days -= cycles * 1461;
    }
    while (days >= 365 + leap(year)) {
        days -= 365 + leap(year);
        year++;
    }
    int month = 1;
    int length;
    while (days >= (length = watch_month_days(year, month))) {
        days -= length;
        month++;
    }
    *date = (watch_date_t){year, month, days + 1, seconds / 3600, seconds / 60 % 60, seconds % 60};
    return true;
}

static int from_bcd(uint8_t value)
{
    return (value & 15) < 10 && (value >> 4) < 10 ? (value >> 4) * 10 + (value & 15) : -1;
}

static uint8_t to_bcd(int value)
{
    return (uint8_t)((value / 10 << 4) | (value % 10));
}

bool watch_pcf_decode(const uint8_t wire[7], uint32_t *utc)
{
    if ((wire[0] & 0x80) || (wire[4] & 7) > 6) {
        return false;
    }
    const watch_date_t date = {
        .year = 2000 + from_bcd(wire[6]), .month = from_bcd(wire[5] & 0x1f),
        .day = from_bcd(wire[3] & 0x3f), .hour = from_bcd(wire[2] & 0x3f),
        .minute = from_bcd(wire[1] & 0x7f), .second = from_bcd(wire[0] & 0x7f),
    };
    return watch_encode_date(&date, 0, utc);
}

bool watch_pcf_encode(uint32_t utc, uint8_t wire[7])
{
    watch_date_t date;
    if (!watch_decode_date(utc, 0, &date)) {
        return false;
    }
    wire[0] = to_bcd(date.second);
    wire[1] = to_bcd(date.minute);
    wire[2] = to_bcd(date.hour);
    wire[3] = to_bcd(date.day);
    wire[4] = (uint8_t)((utc / 86400 + 4) % 7); /* Sunday is zero. */
    wire[5] = to_bcd(date.month);
    wire[6] = to_bcd(date.year - 2000);
    return true;
}
