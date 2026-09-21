/*
 * audio.h - sample format descriptors and PCM sample conversion.
 *
 * All formats are little-endian (as used by WAV files on Windows).
 */
#ifndef PCM_AUDIO_H
#define PCM_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SAMPLE_U8 = 0, /* unsigned 8-bit, silence == 128 */
    SAMPLE_S16,    /* signed 16-bit  */
    SAMPLE_S24,    /* signed 24-bit packed in 3 bytes */
    SAMPLE_S32,    /* signed 32-bit  */
    SAMPLE_F32,    /* IEEE 32-bit float, nominal range [-1, 1] */
    SAMPLE_FORMAT_COUNT
} SampleFormat;

typedef struct {
    uint32_t     sample_rate; /* frames (sample points) per second */
    uint16_t     channels;    /* interleaved channel count */
    SampleFormat format;
} AudioFormat;

int         audio_bytes_per_sample(SampleFormat f);
const char *audio_format_name(SampleFormat f);
int         audio_format_parse(const char *text, SampleFormat *out);
size_t      audio_frame_size(const AudioFormat *fmt);
double      audio_bitrate_kbps(const AudioFormat *fmt);

/*
 * Convert `frames` interleaved frames from `src` (described by `src_fmt`) into
 * `dst` (described by `dst_fmt`), multiplying every sample by `gain`.
 * Both formats must use the same channel count.  Samples are clamped to the
 * valid range of the destination format.  `dst` may alias `src` only when the
 * two formats are identical.
 */
void audio_convert_gain(const void *src, const AudioFormat *src_fmt,
                        void *dst, const AudioFormat *dst_fmt,
                        uint64_t frames, float gain);

#endif /* PCM_AUDIO_H */
