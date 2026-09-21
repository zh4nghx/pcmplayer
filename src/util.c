#include "util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LogLevel g_log_level = LOG_LEVEL_INFO;

void pcm_set_log_level(LogLevel level)
{
    g_log_level = level;
}

static void vlog(const char *tag, const char *fmt, va_list ap)
{
    fputs(tag, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void pcm_log_info(const char *fmt, ...)
{
    va_list ap;
    if (g_log_level < LOG_LEVEL_INFO) return;
    va_start(ap, fmt);
    vlog("[*] ", fmt, ap);
    va_end(ap);
}

void pcm_log_warn(const char *fmt, ...)
{
    va_list ap;
    if (g_log_level < LOG_LEVEL_INFO) return;
    va_start(ap, fmt);
    vlog("[!] ", fmt, ap);
    va_end(ap);
}

void pcm_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("[x] ", fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------------ */

static int parse_suffixed(const char *s, double *out)
{
    double total = 0.0;
    int    any   = 0;
    const char *p = s;

    while (*p) {
        char *end;
        double v;

        while (isspace((unsigned char)*p)) ++p;
        if (!*p) break;

        v = strtod(p, &end);
        if (end == p) return -1;
        p = end;

        while (isspace((unsigned char)*p)) ++p;

        if (p[0] == 'm' && p[1] == 's') {
            total += v * 0.001;
            p += 2;
        } else if (*p == 'h' || *p == 'H') {
            total += v * 3600.0;
            ++p;
        } else if (*p == 'm' || *p == 'M') {
            total += v * 60.0;
            ++p;
        } else if (*p == 's' || *p == 'S') {
            total += v;
            ++p;
        } else {
            return -1;
        }
        any = 1;
    }

    if (!any) return -1;
    *out = total;
    return 0;
}

int pcm_parse_time(const char *text, double *out_seconds)
{
    const char *p;
    double parts[3];
    int    n = 0;

    if (!text) return -1;
    while (isspace((unsigned char)*text)) ++text;
    if (!*text) return -1;

    if (strpbrk(text, "hHmMsS")) {
        double v;
        if (parse_suffixed(text, &v) != 0) return -1;
        if (v < 0) return -1;
        *out_seconds = v;
        return 0;
    }

    p = text;
    while (n < 3) {
        char *end;
        double v;

        v = strtod(p, &end);
        if (end == p) return -1;
        parts[n++] = v;
        p = end;

        while (isspace((unsigned char)*p)) ++p;
        if (*p == ':') { ++p; continue; }
        break;
    }
    if (n == 0) return -1;
    while (isspace((unsigned char)*p)) ++p;
    if (*p) return -1;

    for (int i = 0; i < n; ++i)
        if (parts[i] < 0) return -1;

    if (n == 1)      *out_seconds = parts[0];
    else if (n == 2) *out_seconds = parts[0] * 60.0 + parts[1];
    else             *out_seconds = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    return 0;
}

void pcm_format_time(char *buf, size_t n, double seconds)
{
    long long total_ms, ms, s, m, h;

    if (seconds < 0) seconds = 0;
    total_ms = (long long)(seconds * 1000.0 + 0.5);
    ms = total_ms % 1000;
    total_ms /= 1000;
    s = total_ms % 60;
    total_ms /= 60;
    m = total_ms % 60;
    h = total_ms / 60;

    if (h > 0)
        snprintf(buf, n, "%lld:%02lld:%02lld.%03lld", h, m, s, ms);
    else
        snprintf(buf, n, "%02lld:%02lld.%03lld", m, s, ms);
}

void pcm_format_time_full(char *buf, size_t n, double seconds)
{
    long long total_ms, ms, s, m, h;

    if (seconds < 0) {
        snprintf(buf, n, "--:--.---");
        return;
    }
    total_ms = (long long)(seconds * 1000.0 + 0.5);
    ms = total_ms % 1000;
    total_ms /= 1000;
    s = total_ms % 60;
    total_ms /= 60;
    m = total_ms % 60;
    h = total_ms / 60;

    snprintf(buf, n, "%lld:%02lld:%02lld.%03lld", h, m, s, ms);
}

void pcm_format_count(char *buf, size_t n, unsigned long long value)
{
    char tmp[32];
    size_t len, i, o = 0;

    snprintf(tmp, sizeof(tmp), "%llu", value);
    len = strlen(tmp);
    for (i = 0; i < len && o + 1 < n; ++i) {
        if (i > 0 && ((len - i) % 3) == 0)
            buf[o++] = ',';
        buf[o++] = tmp[i];
    }
    buf[o] = '\0';
}
