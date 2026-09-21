#include "audio.h"

#include <math.h>
#include <string.h>

int audio_bytes_per_sample(SampleFormat f)
{
    switch (f) {
    case SAMPLE_U8:  return 1;
    case SAMPLE_S16: return 2;
    case SAMPLE_S24: return 3;
    case SAMPLE_S32: return 4;
    case SAMPLE_F32: return 4;
    default:         return 0;
    }
}

const char *audio_format_name(SampleFormat f)
{
    switch (f) {
    case SAMPLE_U8:  return "u8";
    case SAMPLE_S16: return "s16le";
    case SAMPLE_S24: return "s24le";
    case SAMPLE_S32: return "s32le";
    case SAMPLE_F32: return "f32le";
    default:         return "unknown";
    }
}

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

int audio_format_parse(const char *text, SampleFormat *out)
{
    static const struct { const char *name; SampleFormat fmt; } table[] = {
        { "u8",        SAMPLE_U8  }, { "u8le",      SAMPLE_U8  },
        { "8",         SAMPLE_U8  }, { "pcm_u8",    SAMPLE_U8  },
        { "s16",       SAMPLE_S16 }, { "s16le",     SAMPLE_S16 },
        { "16",        SAMPLE_S16 }, { "i16",       SAMPLE_S16 },
        { "pcm_s16le", SAMPLE_S16 },
        { "s24",       SAMPLE_S24 }, { "s24le",     SAMPLE_S24 },
        { "24",        SAMPLE_S24 }, { "i24",       SAMPLE_S24 },
        { "s32",       SAMPLE_S32 }, { "s32le",     SAMPLE_S32 },
        { "32",        SAMPLE_S32 }, { "i32",       SAMPLE_S32 },
        { "pcm_s32le", SAMPLE_S32 },
        { "f32",       SAMPLE_F32 }, { "f32le",     SAMPLE_F32 },
        { "float",     SAMPLE_F32 }, { "float32",   SAMPLE_F32 },
        { "32f",       SAMPLE_F32 },
    };

    if (!text || !*text) return -1;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (str_ieq(text, table[i].name)) {
            *out = table[i].fmt;
            return 0;
        }
    }
    return -1;
}

size_t audio_frame_size(const AudioFormat *fmt)
{
    return (size_t)fmt->channels * (size_t)audio_bytes_per_sample(fmt->format);
}

double audio_bitrate_kbps(const AudioFormat *fmt)
{
    return (double)fmt->sample_rate * (double)audio_frame_size(fmt) * 8.0 / 1000.0;
}

/* ------------------------------------------------------------------ */
/* sample <-> float                                                    */
/* ------------------------------------------------------------------ */

static double sample_to_float(const uint8_t *p, SampleFormat f)
{
    switch (f) {
    case SAMPLE_U8:
        return ((double)p[0] - 128.0) / 128.0;

    case SAMPLE_S16: {
        int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        return (double)v / 32768.0;
    }

    case SAMPLE_S24: {
        int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                              ((uint32_t)p[2] << 16));
        if (v & 0x00800000) v |= (int32_t)0xFF000000;
        return (double)v / 8388608.0;
    }

    case SAMPLE_S32: {
        int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                              ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
        return (double)v / 2147483648.0;
    }

    case SAMPLE_F32: {
        float v;
        memcpy(&v, p, sizeof(v));
        return (double)v;
    }

    default:
        return 0.0;
    }
}

static void float_to_sample(uint8_t *p, SampleFormat f, double v)
{
    if (v > 1.0)       v = 1.0;
    else if (v < -1.0) v = -1.0;

    switch (f) {
    case SAMPLE_U8: {
        long t = lrint(v * 127.0) + 128;
        if (t < 0) t = 0; else if (t > 255) t = 255;
        p[0] = (uint8_t)t;
        break;
    }

    case SAMPLE_S16: {
        long t = lrint(v * 32767.0);
        if (t < -32768) t = -32768; else if (t > 32767) t = 32767;
        p[0] = (uint8_t)(t & 0xFF);
        p[1] = (uint8_t)((t >> 8) & 0xFF);
        break;
    }

    case SAMPLE_S24: {
        long t = lrint(v * 8388607.0);
        if (t < -8388608) t = -8388608; else if (t > 8388607) t = 8388607;
        p[0] = (uint8_t)(t & 0xFF);
        p[1] = (uint8_t)((t >> 8) & 0xFF);
        p[2] = (uint8_t)((t >> 16) & 0xFF);
        break;
    }

    case SAMPLE_S32: {
        double d = v * 2147483647.0;
        if (d > 2147483647.0)  d = 2147483647.0;
        if (d < -2147483648.0) d = -2147483648.0;
        int32_t t = (int32_t)llrint(d);
        p[0] = (uint8_t)(t & 0xFF);
        p[1] = (uint8_t)((t >> 8) & 0xFF);
        p[2] = (uint8_t)((t >> 16) & 0xFF);
        p[3] = (uint8_t)((t >> 24) & 0xFF);
        break;
    }

    case SAMPLE_F32: {
        float fv = (float)v;
        memcpy(p, &fv, sizeof(fv));
        break;
    }

    default:
        break;
    }
}

void audio_convert_gain(const void *src, const AudioFormat *src_fmt,
                        void *dst, const AudioFormat *dst_fmt,
                        uint64_t frames, float gain)
{
    if (frames == 0 || src_fmt->channels != dst_fmt->channels)
        return;

    if (src_fmt->format == dst_fmt->format && gain == 1.0f) {
        memcpy(dst, src, (size_t)(frames * audio_frame_size(dst_fmt)));
        return;
    }

    const uint8_t *s = (const uint8_t *)src;
    uint8_t       *d = (uint8_t *)dst;
    const size_t   sb = (size_t)audio_bytes_per_sample(src_fmt->format);
    const size_t   db = (size_t)audio_bytes_per_sample(dst_fmt->format);
    const uint64_t n  = frames * dst_fmt->channels;

    for (uint64_t i = 0; i < n; ++i)
        float_to_sample(d + i * db, dst_fmt->format,
                        sample_to_float(s + i * sb, src_fmt->format) * (double)gain);
}
