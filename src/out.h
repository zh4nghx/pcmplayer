/*
 * out.h - audio output device (WASAPI shared mode, event driven).
 */
#ifndef PCM_OUT_H
#define PCM_OUT_H

#include <stddef.h>
#include <stdint.h>

#include "audio.h"

typedef struct AudioOut AudioOut;

/*
 * Called from the render thread to fill `frames` frames of PCM data in
 * the format passed to aout_open().  Must write exactly
 * frames * frame_size bytes (silence when no data is available).
 */
typedef void (*AoutFillFn)(void *user, uint8_t *dst, uint32_t frames);

AudioOut *aout_open(const AudioFormat *fmt, uint32_t buffer_ms,
                    AoutFillFn fill, void *user,
                    char *err, size_t errlen);
int      aout_start(AudioOut *a);   /* begin pulling data from the callback */

/* Discard everything queued in the device (call after a seek). */
void     aout_flush(AudioOut *a);

uint32_t aout_buffer_frames(const AudioOut *a); /* total device buffer size */
uint32_t aout_queued_frames(const AudioOut *a);  /* frames not played yet    */
void     aout_close(AudioOut *a);

#endif /* PCM_OUT_H */
