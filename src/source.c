/*
 * source.c - PCM data source: RIFF/WAVE files and headerless raw PCM.
 *
 * File I/O uses the native Win32 API (CreateFileA / ReadFile /
 * SetFilePointerEx / GetFileSizeEx) rather than the C stdio layer.  This
 * keeps position accounting exact across CRT implementations (msvcrt.dll
 * on modern Windows no longer exports _ftelli64, and mixing _telli64 on
 * the underlying fd with stdio buffering would report buffered offsets).
 */
#include "source.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wincompat.h"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#define FOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))

#define ID_RIFF FOURCC('R', 'I', 'F', 'F')
#define ID_RIFX FOURCC('R', 'I', 'F', 'X')
#define ID_RFX  FOURCC('R', 'F', '6', '4')
#define ID_WAVE FOURCC('W', 'A', 'V', 'E')
#define ID_FMT  FOURCC('f', 'm', 't', ' ')
#define ID_DATA FOURCC('d', 'a', 't', 'a')

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM        0x0001
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#define WAVE_FORMAT_ALAW       0x0006
#define WAVE_FORMAT_MULAW      0x0007
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

struct PcmSource {
    HANDLE     fh;            /* INVALID_HANDLE_VALUE when closed */
    char      *path;
    int        is_wav;
    AudioFormat fmt;

    int64_t  data_offset;  /* byte offset of the first sample */
    uint64_t data_bytes;   /* length of the sample payload */
    size_t   frame_size;   /* bytes per frame */

    uint64_t frames_total; /* frames playable from position 0 */
    uint64_t frame_pos;    /* next frame to be read */
};

/* ------------------------------------------------------------------ */
/* low level I/O helpers                                               */
/* ------------------------------------------------------------------ */

static size_t io_read(PcmSource *s, void *dst, size_t bytes)
{
    size_t done = 0;
    while (done < bytes) {
        DWORD want = (bytes - done > 0xFFFFFFFFu)
                         ? 0xFFFFFFFFu : (DWORD)(bytes - done);
        DWORD got = 0;
        if (!ReadFile(s->fh, (char *)dst + done, want, &got, NULL) || got == 0)
            break;
        done += got;
    }
    return done;
}

static int io_seek_abs(PcmSource *s, int64_t off)
{
    LARGE_INTEGER li;
    li.QuadPart = off;
    return SetFilePointerEx(s->fh, li, NULL, FILE_BEGIN) ? 0 : -1;
}

static int64_t io_tell(PcmSource *s)
{
    LARGE_INTEGER zero, cur;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(s->fh, zero, &cur, FILE_CURRENT))
        return -1;
    return (int64_t)cur.QuadPart;
}

static int io_size(PcmSource *s, uint64_t *out)
{
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(s->fh, &sz))
        return -1;
    *out = (uint64_t)sz.QuadPart;
    return 0;
}

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void set_err(char *err, size_t errlen, const char *fmt, ...)
{
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static char *dup_str(const char *src)
{
    size_t n = strlen(src) + 1;
    char  *p = (char *)malloc(n);
    if (p) memcpy(p, src, n);
    return p;
}

static int format_from_wave(uint32_t tag, uint16_t bits, SampleFormat *out)
{
    if (tag == WAVE_FORMAT_PCM) {
        switch (bits) {
        case 8:  *out = SAMPLE_U8;  return 0;
        case 16: *out = SAMPLE_S16; return 0;
        case 24: *out = SAMPLE_S24; return 0;
        case 32: *out = SAMPLE_S32; return 0;
        default: return -1;
        }
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) {
        *out = SAMPLE_F32;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* WAV parsing                                                         */
/* ------------------------------------------------------------------ */

static int parse_wav(PcmSource *s, const char *path, char *err, size_t errlen)
{
    uint8_t   hdr[12];
    uint32_t  tag = 0, rate = 0;
    uint16_t  channels = 0, bits = 0;
    int       have_fmt = 0, have_data = 0;
    long long guard = 0;

    if (io_read(s, hdr, sizeof(hdr)) != sizeof(hdr)) {
        set_err(err, errlen, "%s: file is too short to be a WAV file", path);
        return -1;
    }
    if (rd32le(hdr) == ID_RIFX) {
        set_err(err, errlen, "%s: big-endian (RIFX) WAV files are not supported",
                path);
        return -1;
    }
    if (rd32le(hdr) != ID_RIFF || rd32le(hdr + 8) != ID_WAVE) {
        set_err(err, errlen, "%s: not a RIFF/WAVE file", path);
        return -1;
    }
    if (io_seek_abs(s, 12) != 0) {
        set_err(err, errlen, "%s: cannot seek past the RIFF header", path);
        return -1;
    }

    while (guard++ < 4096) {
        uint8_t  ch[8];
        uint32_t id, size;
        int64_t  body;

        if (io_read(s, ch, sizeof(ch)) != sizeof(ch))
            break;

        id   = rd32le(ch);
        size = rd32le(ch + 4);
        body = io_tell(s);
        if (body < 0)
            break;

        if (id == ID_FMT) {
            uint8_t  fmtbuf[64];
            uint32_t want = size;

            if (want < 16) {
                set_err(err, errlen, "%s: malformed 'fmt ' chunk", path);
                return -1;
            }
            if (want > sizeof(fmtbuf))
                want = sizeof(fmtbuf);
            if (io_read(s, fmtbuf, want) != want) {
                set_err(err, errlen, "%s: truncated 'fmt ' chunk", path);
                return -1;
            }

            tag      = rd16le(fmtbuf + 0);
            channels = rd16le(fmtbuf + 2);
            rate     = rd32le(fmtbuf + 4);
            bits     = rd16le(fmtbuf + 14);

            if (tag == WAVE_FORMAT_EXTENSIBLE) {
                if (want < 40) {
                    set_err(err, errlen,
                            "%s: WAVE_FORMAT_EXTENSIBLE header is truncated", path);
                    return -1;
                }
                tag = rd16le(fmtbuf + 24); /* SubFormat GUID, first word */
            }
            have_fmt = 1;
        } else if (id == ID_DATA && !have_data) {
            s->data_offset = body;
            s->data_bytes  = size;
            have_data      = 1;

            if (size == 0 || size == 0xFFFFFFFFu) {
                /* size unknown (streamed WAV): measure the real tail */
                uint64_t total;
                if (io_size(s, &total) == 0 && total > (uint64_t)body)
                    s->data_bytes = total - (uint64_t)body;
                else
                    s->data_bytes = 0;
            }
        }

        if (have_fmt && have_data)
            break;

        /* advance to the next chunk (chunks are word aligned) */
        if (io_seek_abs(s, body + (int64_t)size + (int64_t)(size & 1u)) != 0)
            break;
    }

    if (!have_fmt || !have_data) {
        set_err(err, errlen, "%s: WAV file has no '%s' chunk", path,
                have_fmt ? "data" : "fmt ");
        return -1;
    }
    if (channels == 0 || rate == 0) {
        set_err(err, errlen, "%s: WAV header declares %u channel(s) at %u Hz",
                path, (unsigned)channels, rate);
        return -1;
    }
    if (tag == WAVE_FORMAT_ALAW || tag == WAVE_FORMAT_MULAW) {
        set_err(err, errlen,
                "%s: companded WAV (A-law/mu-law) is not supported", path);
        return -1;
    }
    if (format_from_wave(tag, bits, &s->fmt.format) != 0) {
        set_err(err, errlen,
                "%s: unsupported WAV encoding (format tag 0x%04X, %u bits)",
                path, (unsigned)tag, (unsigned)bits);
        return -1;
    }

    s->fmt.sample_rate = rate;
    s->fmt.channels    = channels;
    return 0;
}

/* ------------------------------------------------------------------ */
/* raw PCM                                                             */
/* ------------------------------------------------------------------ */

static int parse_raw(PcmSource *s, const SourceOptions *opts,
                     const char *path, char *err, size_t errlen)
{
    uint64_t size;

    if (io_size(s, &size) != 0) {
        set_err(err, errlen, "%s: cannot determine the file size", path);
        return -1;
    }
    if (size == 0) {
        set_err(err, errlen, "%s: file is empty", path);
        return -1;
    }
    if (io_seek_abs(s, 0) != 0) {
        set_err(err, errlen, "%s: cannot rewind the file", path);
        return -1;
    }

    s->fmt.sample_rate = opts->raw_rate;
    s->fmt.channels    = opts->raw_channels;
    s->fmt.format      = opts->raw_format;
    s->data_offset     = 0;
    s->data_bytes      = size;
    return 0;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

PcmSource *source_open(const char *path, const SourceOptions *opts,
                       char *err, size_t errlen)
{
    SourceOptions defaults;
    PcmSource    *s;
    uint8_t       magic[4];
    size_t        magic_len;

    if (!path || !*path) {
        set_err(err, errlen, "no input file given");
        return NULL;
    }

    if (!opts) {
        memset(&defaults, 0, sizeof(defaults));
        defaults.raw_rate     = 44100;
        defaults.raw_channels = 2;
        defaults.raw_format   = SAMPLE_S16;
        opts = &defaults;
    }

    s = (PcmSource *)calloc(1, sizeof(*s));
    if (!s) {
        set_err(err, errlen, "out of memory");
        return NULL;
    }
    s->fh = INVALID_HANDLE_VALUE;
    s->path = dup_str(path);
    if (!s->path) {
        set_err(err, errlen, "out of memory");
        source_close(s);
        return NULL;
    }

    s->fh = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s->fh == INVALID_HANDLE_VALUE) {
        set_err(err, errlen, "%s: cannot open file for reading", path);
        source_close(s);
        return NULL;
    }

    magic_len = io_read(s, magic, sizeof(magic));
    if (io_seek_abs(s, 0) != 0) {
        set_err(err, errlen, "%s: cannot rewind the file", path);
        source_close(s);
        return NULL;
    }

    s->is_wav = (!opts->raw && magic_len == 4 && rd32le(magic) == ID_RIFF);

    if (s->is_wav) {
        if (parse_wav(s, path, err, errlen) != 0) {
            source_close(s);
            return NULL;
        }
    } else if (opts->raw || magic_len == 0 ||
               rd32le(magic) != ID_RIFF) {
        if (parse_raw(s, opts, path, err, errlen) != 0) {
            source_close(s);
            return NULL;
        }
    }

    s->frame_size = (size_t)s->fmt.channels *
                    (size_t)audio_bytes_per_sample(s->fmt.format);
    if (s->frame_size == 0) {
        set_err(err, errlen, "%s: invalid frame size", path);
        source_close(s);
        return NULL;
    }

    s->frames_total = s->data_bytes / (uint64_t)s->frame_size;
    if (s->frames_total == 0) {
        set_err(err, errlen, "%s: the file contains no complete audio frame", path);
        source_close(s);
        return NULL;
    }

    if (opts->duration_sec > 0) {
        uint64_t limit =
            (uint64_t)(opts->duration_sec * (double)s->fmt.sample_rate + 0.5);
        if (limit > 0 && limit < s->frames_total)
            s->frames_total = limit;
    }

    if (opts->start_sec > 0) {
        uint64_t start =
            (uint64_t)(opts->start_sec * (double)s->fmt.sample_rate + 0.5);
        if (start >= s->frames_total) {
            set_err(err, errlen,
                    "%s: start position is past the end of the audio", path);
            source_close(s);
            return NULL;
        }
        if (source_seek_frame(s, start) != 0) {
            set_err(err, errlen, "%s: cannot seek to the requested position",
                    path);
            source_close(s);
            return NULL;
        }
    }

    return s;
}

void source_close(PcmSource *s)
{
    if (!s) return;
    if (s->fh && s->fh != INVALID_HANDLE_VALUE)
        CloseHandle(s->fh);
    free(s->path);
    free(s);
}

int                source_is_wav(const PcmSource *s)  { return s->is_wav; }
const char        *source_path(const PcmSource *s)    { return s->path; }
const AudioFormat *source_format(const PcmSource *s)  { return &s->fmt; }
uint64_t           source_frame_count(const PcmSource *s) { return s->frames_total; }
uint64_t           source_frame_pos(const PcmSource *s)   { return s->frame_pos; }

uint32_t source_read_frames(PcmSource *s, void *dst, uint32_t frames)
{
    uint64_t avail;
    size_t   want, got;
    uint32_t n;

    if (!s || frames == 0) return 0;

    avail = (s->frame_pos < s->frames_total) ? s->frames_total - s->frame_pos : 0;
    if ((uint64_t)frames > avail)
        frames = (uint32_t)avail;
    if (frames == 0) return 0;

    want = (size_t)frames * s->frame_size;
    got  = io_read(s, dst, want);
    n    = (uint32_t)(got / s->frame_size);
    s->frame_pos += n;

    if (got % s->frame_size) {
        /* trailing partial frame: rewind to the frame boundary */
        io_seek_abs(s, s->data_offset + (int64_t)(s->frame_pos * s->frame_size));
    }
    return n;
}

int source_seek_frame(PcmSource *s, uint64_t frame)
{
    if (!s) return -1;
    if (frame > s->frames_total) frame = s->frames_total;
    if (io_seek_abs(s, s->data_offset + (int64_t)(frame * s->frame_size)) != 0)
        return -1;
    s->frame_pos = frame;
    return 0;
}
