/*
 * source.h - PCM data source: RIFF/WAVE files and headerless raw PCM.
 */
#ifndef PCM_SOURCE_H
#define PCM_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "audio.h"

typedef struct {
    int          raw;           /* force headerless PCM interpretation */
    uint32_t     raw_rate;      /* used for raw input only */
    uint16_t     raw_channels;  /* used for raw input only */
    SampleFormat raw_format;    /* used for raw input only */
    double       start_sec;     /* skip that much audio at open time */
    double       duration_sec;  /* limit playback length, <= 0 means all */
} SourceOptions;

typedef struct PcmSource PcmSource;

PcmSource *source_open(const char *path, const SourceOptions *opts,
                       char *err, size_t errlen);
void source_close(PcmSource *s);

int                source_is_wav(const PcmSource *s);
const char        *source_path(const PcmSource *s);
const AudioFormat *source_format(const PcmSource *s);

uint64_t source_frame_count(const PcmSource *s);  /* frames available from 0 */
uint64_t source_frame_pos(const PcmSource *s);
uint32_t source_read_frames(PcmSource *s, void *dst, uint32_t frames);
int      source_seek_frame(PcmSource *s, uint64_t frame);

#endif /* PCM_SOURCE_H */
