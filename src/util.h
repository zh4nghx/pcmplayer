/*
 * util.h - small helpers: version, diagnostics, time parsing/formatting.
 */
#ifndef PCM_UTIL_H
#define PCM_UTIL_H

#include <stddef.h>

#define PCM_VERSION "1.0.0"

typedef enum {
    LOG_LEVEL_QUIET = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} LogLevel;

void pcm_set_log_level(LogLevel level);

void pcm_log_info(const char *fmt, ...);
void pcm_log_warn(const char *fmt, ...);
void pcm_log_error(const char *fmt, ...);

/* Parse "12.5", "1:30", "1:02:03.5", "1h2m3s", "500ms" into seconds. */
int pcm_parse_time(const char *text, double *out_seconds);

/* "MM:SS.mmm" or "H:MM:SS.mmm" for values >= 1 hour. */
void pcm_format_time(char *buf, size_t n, double seconds);

/* "H:MM:SS.mmm" always. Handles negative values as "--:--.---". */
void pcm_format_time_full(char *buf, size_t n, double seconds);

/* Human readable byte/frame counts. */
void pcm_format_count(char *buf, size_t n, unsigned long long value);

#endif /* PCM_UTIL_H */
