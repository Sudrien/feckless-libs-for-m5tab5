/* SPDX-License-Identifier: MIT
 *
 * ESP-IDF's log calls, for a boxed app. They are varargs with a format the
 * host must not walk -- a %s would be a pointer of the program's -- so the
 * line is formatted here, with this component's vsnprintf, and the host is
 * handed two strings it checks: the tag and the finished message.
 *
 * esp_log() (IDF 6's, behind ESP_LOGx) passes a config word whose low bits
 * are the level and whose flags say whether the host is to add the colour,
 * timestamp and tag; it is passed on as it is. esp_log_write() comes with
 * its prefix already in the format, so the host writes the message alone.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define BOX_LOG_LINE 256

/* The host's (feckless_box_calls): kind 0 = esp_log, config word in cfg;
 * kind 1 = esp_log_write, the level in cfg. */
extern void feckless_box_log(uint32_t kind, uint32_t cfg, const char *tag, const char *msg);

static void out(uint32_t kind, uint32_t cfg, const char *tag, const char *fmt, va_list ap)
{
    char line[BOX_LOG_LINE];
    vsnprintf(line, sizeof line, fmt, ap);
    feckless_box_log(kind, cfg, tag ? tag : "", line);
}

/* esp_log_config_t is one 32-bit word, passed in a0 as a uint32_t would be. */
void esp_log_va(uint32_t config, const char *tag, const char *fmt, va_list ap)
{
    out(0, config, tag, fmt, ap);
}

void esp_log(uint32_t config, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    out(0, config, tag, fmt, ap);
    va_end(ap);
}

void esp_log_writev(uint32_t level, const char *tag, const char *fmt, va_list ap)
{
    out(1, level, tag, fmt, ap);
}

void esp_log_write(uint32_t level, const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    out(1, level, tag, fmt, ap);
    va_end(ap);
}
