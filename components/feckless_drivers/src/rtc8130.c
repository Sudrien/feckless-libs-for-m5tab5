/*
 * rtc8130.c -- see rtc8130.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rtc8130.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "tab5_rtc";

#define RTC_ADDR        (0x32)
#define RTC_TIMEOUT_MS  (100)

#define REG_SEC         (0x10)      /* ..0x16: sec min hour wday mday month year */
#define REG_ALARM       (0x17)      /* ..0x19: minute, hour, weekday/day (9014) */
#define REG_EXT         (0x1C)
#define REG_FLAG        (0x1D)
#define REG_CTRL0       (0x1E)
#define REG_CTRL1       (0x1F)

#define FLAG_VLF        (1u << 1)   /* oscillator stop / backup voltage low */
#define FLAG_AF         (1u << 3)   /* 9014: the alarm came due */
#define FLAG_TF         (1u << 4)
#define FLAG_UF         (1u << 5)
#define EXT_WADA        (1u << 3)   /* 0: the day register is a weekday */
#define EXT_TE          (1u << 4)
#define ALARM_AE        (0x80)      /* in an alarm register: ignore it */
#define CTRL0_STOP      (1u << 6)   /* hold the counters while they are written */
#define CTRL0_UIE       (1u << 5)
#define CTRL0_TIE       (1u << 4)
#define CTRL0_AIE       (1u << 3)
#define CTRL1_BACKUP    ((1u << 4) | (1u << 5))     /* as M5Stack's initBat() */

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t       s_lock;

/* ---- calendar, pure ------------------------------------------------ */

static uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

/* Days from 1970-01-01 to y-m-d, proleptic Gregorian (H. Hinnant's). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp  = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

/* ---- the bus ------------------------------------------------------- */

static bool rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, RTC_TIMEOUT_MS) == ESP_OK;
}

static bool wr(uint8_t reg, const uint8_t *buf, size_t n)
{
    uint8_t b[8];
    if (n > sizeof(b) - 1) return false;
    b[0] = reg;
    for (size_t i = 0; i < n; i++) b[i + 1] = buf[i];
    return i2c_master_transmit(s_dev, b, n + 1, RTC_TIMEOUT_MS) == ESP_OK;
}

bool rtc8130_init(i2c_master_bus_handle_t bus)
{
    if (s_dev || !bus) return s_dev != NULL;
    if (i2c_master_probe(bus, RTC_ADDR, RTC_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "RX8130 not found at 0x%02X; the clock will not survive a power-off",
                 RTC_ADDR);
        return false;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = RTC_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &cfg, &s_dev) != ESP_OK) {
        s_dev = NULL;
        return false;
    }
    s_lock = xSemaphoreCreateMutex();
    uint8_t c1 = 0;
    if (rd(REG_CTRL1, &c1, 1) && (c1 & CTRL1_BACKUP) != CTRL1_BACKUP) {
        c1 |= CTRL1_BACKUP;
        (void)wr(REG_CTRL1, &c1, 1);
    }
    ESP_LOGI(TAG, "RX8130 at 0x%02X (backup 0x%02X)", RTC_ADDR, c1);
    return true;
}

static bool read_locked(int64_t *epoch)
{
    uint8_t flag = 0, t[7];
    if (!rd(REG_FLAG, &flag, 1) || !rd(REG_SEC, t, sizeof(t))) return false;
    if (flag & FLAG_VLF) return false;

    const int sec = bcd2bin(t[0] & 0x7F), min = bcd2bin(t[1] & 0x7F);
    const int hour = bcd2bin(t[2] & 0x3F), mday = bcd2bin(t[4] & 0x3F);
    const int mon = bcd2bin(t[5] & 0x1F), year = 2000 + bcd2bin(t[6]);
    if (sec > 59 || min > 59 || hour > 23 || mday < 1 || mday > 31 ||
        mon < 1 || mon > 12) {
        return false;
    }
    *epoch = days_from_civil(year, mon, mday) * 86400 + hour * 3600 + min * 60 + sec;
    return true;
}

bool rtc8130_read(int64_t *epoch)
{
    if (!s_dev || !epoch) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = read_locked(epoch);
    xSemaphoreGive(s_lock);
    return ok;
}

static bool write_locked(int64_t epoch)
{
    const int64_t days = epoch / 86400;
    const int secs = (int)(epoch % 86400);
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    if (y < 2000 || y > 2099) return false;
    const int wday = (int)((days + 4) % 7);         /* 1970-01-01 was a Thursday */

    const uint8_t t[7] = {
        bin2bcd(secs % 60), bin2bcd(secs / 60 % 60), bin2bcd(secs / 3600),
        (uint8_t)(1u << wday), bin2bcd(d), bin2bcd(m), bin2bcd(y - 2000),
    };
    uint8_t c0 = 0;
    if (!rd(REG_CTRL0, &c0, 1)) return false;
    const uint8_t stop = (uint8_t)(c0 | CTRL0_STOP), run = (uint8_t)(c0 & ~CTRL0_STOP);
    bool ok = wr(REG_CTRL0, &stop, 1) && wr(REG_SEC, t, sizeof(t));
    ok = wr(REG_CTRL0, &run, 1) && ok;             /* restart it whatever happened */
    uint8_t flag = 0;
    if (ok && rd(REG_FLAG, &flag, 1) && (flag & FLAG_VLF)) {
        flag &= (uint8_t)~FLAG_VLF;
        ok = wr(REG_FLAG, &flag, 1);
    }
    return ok;
}

bool rtc8130_write(int64_t epoch)
{
    if (!s_dev) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = write_locked(epoch);
    xSemaphoreGive(s_lock);
    if (ok) ESP_LOGI(TAG, "RX8130 set to %" PRId64, epoch);
    return ok;
}

bool rtc8130_write_forward(int64_t epoch)
{
    if (!s_dev) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t held = 0;
    const bool valid = read_locked(&held);
    const bool ok = (!valid || epoch > held + 2) ? write_locked(epoch) : true;
    xSemaphoreGive(s_lock);
    if (ok && (!valid || epoch > held + 2)) {
        ESP_LOGI(TAG, "RX8130 moved forward to %" PRId64 " (held %s)", epoch,
                 valid ? "an earlier time" : "nothing valid");
    }
    return ok;
}

/* ---- the alarm (9014) ------------------------------------------------ */

/* AF and AIE as asked, TIE/UIE/TE off whatever they were. Locked. */
static bool alarm_state_locked(bool aie)
{
    uint8_t c0 = 0, ext = 0, flag = 0;
    if (!rd(REG_CTRL0, &c0, 1) || !rd(REG_EXT, &ext, 1) || !rd(REG_FLAG, &flag, 1)) return false;
    c0 = (uint8_t)(c0 & ~(CTRL0_AIE | CTRL0_TIE | CTRL0_UIE));
    if (aie) c0 |= CTRL0_AIE;
    ext = (uint8_t)(ext & ~(EXT_TE | EXT_WADA));
    flag = (uint8_t)(flag & ~(FLAG_AF | FLAG_TF | FLAG_UF));
    /* Off and the flag cleared first, so a match during the write
     * cannot leave AF set behind it. */
    const uint8_t off = (uint8_t)(c0 & ~CTRL0_AIE);
    return wr(REG_CTRL0, &off, 1) && wr(REG_EXT, &ext, 1) && wr(REG_FLAG, &flag, 1) &&
           (!aie || wr(REG_CTRL0, &c0, 1));
}

bool rtc8130_alarm_take(void)
{
    if (!s_dev) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t flag = 0;
    const bool read = rd(REG_FLAG, &flag, 1);
    const bool ok = alarm_state_locked(false);
    xSemaphoreGive(s_lock);
    const bool woke = read && (flag & FLAG_AF);
    ESP_LOGI(TAG, "alarm %s (FLAG 0x%02X)%s", woke ? "was due: a scheduled wake" : "not due",
             flag, ok ? "" : "; could not clear it");
    return woke;
}

bool rtc8130_alarm_arm(uint32_t secs)
{
    if (!s_dev) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t t[3];
    bool ok = rd(REG_SEC, t, sizeof(t));
    int at = 0;
    if (ok) {
        const int sec = bcd2bin(t[0] & 0x7F), min = bcd2bin(t[1] & 0x7F), hour = bcd2bin(t[2] & 0x3F);
        ok = sec <= 59 && min <= 59 && hour <= 23;
        uint32_t mins = (secs + (uint32_t)sec + 59u) / 60u;    /* up to the minute */
        if (mins < 1) mins = 1;
        if (mins > 1439) mins = 1439;                           /* the day is ignored */
        at = (int)((hour * 60 + min + (int)mins) % 1440);
    }
    if (ok) {
        ok = alarm_state_locked(false);     /* AF cleared, AIE off while it is written */
        const uint8_t a[3] = { bin2bcd(at % 60), bin2bcd(at / 60), ALARM_AE };
        ok = ok && wr(REG_ALARM, a, sizeof(a)) && alarm_state_locked(true);
    }
    xSemaphoreGive(s_lock);
    if (ok) ESP_LOGI(TAG, "alarm armed for %02d:%02d on the RTC, %" PRIu32 " s on", at / 60, at % 60, secs);
    else    ESP_LOGW(TAG, "alarm not armed");
    return ok;
}

bool rtc8130_alarm_off(void)
{
    if (!s_dev) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = alarm_state_locked(false);
    xSemaphoreGive(s_lock);
    return ok;
}
