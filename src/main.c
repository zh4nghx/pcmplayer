/*
 * main.c - pcmplay entry point: command line parsing, playback loop and
 * the interactive console UI.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include "wincompat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "console.h"
#include "out.h"
#include "source.h"
#include "util.h"

#define VOLUME_MIN     0.0f
#define VOLUME_MAX     2.0f
#define VOLUME_STEP    0.05f
#define SEEK_SMALL     5.0    /* seconds */
#define SEEK_BIG       30.0   /* seconds */
#define UI_REFRESH_MS  66
#define UI_LINES       3

typedef struct {
    SourceOptions sopts;
    float  volume;   /* 1.0 == 100% */
    int    quiet;
    int    debug;
} Options;

typedef struct {
    PcmSource  *src;
    AudioFormat fmt;
    size_t      frame_size;
    float       volume;
    int         paused;
    int         eof;
    uint64_t    frames_filled;   /* frames handed to the device          */
    uint8_t    *scratch;         /* fill buffer in the source format     */
    AudioOut   *aout;
    SRWLOCK     lock;            /* guards src/volume/paused/eof/filled  */
} Player;

typedef struct {
    uint64_t pos;
    uint64_t total;
    uint64_t queued;
    int      paused;
    int      eof;
    int      volume_pct;
} PlayerSnap;

enum { PLAY_DONE = 0, PLAY_SKIP, PLAY_QUIT, PLAY_ERROR };

/* ------------------------------------------------------------------ */
/* player core                                                         */
/* ------------------------------------------------------------------ */

static void player_fill(void *user, uint8_t *dst, uint32_t frames)
{
    Player *p   = (Player *)user;
    size_t  bs  = (size_t)frames * p->frame_size;

    AcquireSRWLockExclusive(&p->lock);

    if (p->paused || p->eof) {
        memset(dst, 0, bs);
    } else {
        uint32_t got = source_read_frames(p->src, p->scratch, frames);

        if (got > 0)
            audio_convert_gain(p->scratch, &p->fmt, dst, &p->fmt, got,
                               p->volume);
        if (got < frames)
            memset(dst + (size_t)got * p->frame_size, 0,
                   bs - (size_t)got * p->frame_size);
        if (got < frames)
            p->eof = 1;
        p->frames_filled += got;
    }

    ReleaseSRWLockExclusive(&p->lock);
}

static void player_snapshot(Player *p, PlayerSnap *s)
{
    AcquireSRWLockShared(&p->lock);
    s->queued      = aout_queued_frames(p->aout);
    s->pos         = (s->queued < p->frames_filled)
                         ? p->frames_filled - s->queued : 0;
    s->paused      = p->paused;
    s->eof         = p->eof;
    s->volume_pct  = (int)(p->volume * 100.0f + 0.5f);
    ReleaseSRWLockShared(&p->lock);

    s->total = source_frame_count(p->src);
    if (s->pos > s->total)
        s->pos = s->total;
}

static void player_seek_to(Player *p, uint64_t frame)
{
    uint64_t total = source_frame_count(p->src);
    if (frame > total)
        frame = total;

    AcquireSRWLockExclusive(&p->lock);
    source_seek_frame(p->src, frame);
    p->frames_filled = frame;
    p->eof           = (frame >= total);
    ReleaseSRWLockExclusive(&p->lock);

    aout_flush(p->aout);
}

static void player_seek_by(Player *p, double seconds)
{
    uint32_t rate  = p->fmt.sample_rate;
    uint64_t total = source_frame_count(p->src);
    double   cur_s, target;

    {
        uint64_t cur;
        AcquireSRWLockShared(&p->lock);
        {
            uint64_t queued = aout_queued_frames(p->aout);
            cur = (queued < p->frames_filled) ? p->frames_filled - queued : 0;
        }
        ReleaseSRWLockShared(&p->lock);
        cur_s = (double)cur / (double)rate;
    }

    target = cur_s + seconds;
    if (target < 0.0)
        target = 0.0;
    {
        double total_s = (double)total / (double)rate;
        if (target > total_s)
            target = total_s;
    }

    player_seek_to(p, (uint64_t)(target * (double)rate + 0.5));
}

static void player_volume(Player *p, float delta)
{
    AcquireSRWLockExclusive(&p->lock);
    p->volume += delta;
    if (p->volume < VOLUME_MIN) p->volume = VOLUME_MIN;
    if (p->volume > VOLUME_MAX) p->volume = VOLUME_MAX;
    ReleaseSRWLockExclusive(&p->lock);
}

static void player_toggle_pause(Player *p)
{
    AcquireSRWLockExclusive(&p->lock);
    p->paused = !p->paused;
    ReleaseSRWLockExclusive(&p->lock);
}

/* ------------------------------------------------------------------ */
/* interactive UI                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int base_y;
    int width;
} UiState;

static void print_header(PcmSource *src)
{
    const AudioFormat *f = source_format(src);
    char tbuf[32], nbuf[32];

    pcm_format_time_full(tbuf, sizeof(tbuf),
                         (double)source_frame_count(src) / f->sample_rate);
    pcm_format_count(nbuf, sizeof(nbuf), source_frame_count(src));

    con_set_color(CON_BOLD);
    printf(" %s", source_path(src));
    con_set_color(CON_DIM);
    printf("  [%s]", source_is_wav(src) ? "WAV" : "raw PCM");
    con_reset_color();
    printf("\n   %u Hz / %u ch / %s / %.0f kbps / %s / %s frames\n\n",
           f->sample_rate, f->channels, audio_format_name(f->format),
           audio_bitrate_kbps(f), tbuf, nbuf);
    fflush(stdout);
}

static void ui_print_row(int y, int w, const char *text, ConsoleColor color)
{
    int len = (int)strlen(text);

    con_gotoxy(0, y);
    con_set_color(color);
    printf("%.*s", w, text);
    if (len < w)
        printf("%*s", w - len, "");
    con_reset_color();
}

static void ui_draw(Player *p, UiState *ui)
{
    PlayerSnap s;
    char  line[1024];
    char  t1[32], t2[32];
    char  bar[1100];
    char  volbar[16];
    int   x, y, w, bar_w, fill, o, i;

    player_snapshot(p, &s);

    /* repaint the header when this is the first draw or the buffer scrolled */
    con_getxy(&x, &y);
    if (y != ui->base_y + UI_LINES) {
        con_gotoxy(0, y);
        print_header(p->src);
        con_getxy(NULL, &y);
        ui->base_y = y;
    }

    w = ui->width;

    pcm_format_time_full(t1, sizeof(t1),
                         (double)s.pos / (double)p->fmt.sample_rate);
    pcm_format_time_full(t2, sizeof(t2),
                         (double)s.total / (double)p->fmt.sample_rate);

    bar_w = w - (int)(strlen(t1) + strlen(t2)) - 6;
    if (bar_w < 8)        bar_w = 8;
    if (bar_w > 1000)     bar_w = 1000;

    fill = (int)((double)s.pos / (double)(s.total ? s.total : 1) * bar_w + 0.5);
    if (fill > bar_w) fill = bar_w;

    o = 0;
    bar[o++] = ' ';
    bar[o++] = '[';
    for (i = 0; i < bar_w; ++i) {
        if (i < fill)                         bar[o++] = '#';
        else if (i == fill && s.pos < s.total) bar[o++] = '>';
        else                                   bar[o++] = '.';
    }
    bar[o++] = ']';
    bar[o]   = '\0';

    snprintf(line, sizeof(line), "%s  %s / %s", bar, t1, t2);
    ui_print_row(ui->base_y, w, line, s.eof ? CON_DIM : CON_ACCENT);

    {
        const char *state = s.paused ? "paused"
                                     : (s.eof ? "end of file" : "playing");
        int vfill = s.volume_pct / 10;   /* 10 blocks of 10% */
        if (vfill > 10) vfill = 10;

        o = 0;
        volbar[o++] = '[';
        for (i = 0; i < 10; ++i)
            volbar[o++] = (i < vfill) ? '#' : '-';
        volbar[o++] = ']';
        volbar[o]   = '\0';

        snprintf(line, sizeof(line), " %s  |  volume %s %d%%",
                 state, volbar, s.volume_pct);
    }
    ui_print_row(ui->base_y + 1, w, line,
                 s.paused ? CON_WARN : (s.eof ? CON_DIM : CON_DEFAULT));

    if (s.eof)
        snprintf(line, sizeof(line),
                 " end of file  |  [r] replay  [n] next  [q] quit");
    else
        snprintf(line, sizeof(line),
                 " [space] pause  [left/right] -/+5s  [pgup/pgdn] -/+30s"
                 "  [up/down] volume  [home/end] jump  [r] restart"
                 "  [n] next  [q] quit");
    ui_print_row(ui->base_y + 2, w, line, CON_DIM);

    con_gotoxy(0, ui->base_y + UI_LINES);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* playback loop                                                       */
/* ------------------------------------------------------------------ */

static int run_loop(Player *p, const Options *o)
{
    int interactive = (!o->quiet && con_available());
    UiState ui;
    int     rc = PLAY_DONE;

    ui.base_y = -1;
    ui.width  = con_width() - 1;
    if (ui.width < 40) ui.width = 40;

    if (interactive) {
        con_show_cursor(0);
        con_drain_keys();
    }

    for (;;) {
        if (interactive) {
            int key;
            while ((key = con_read_key()) != CON_KEY_NONE) {
                switch (key) {
                case 'q': case 'Q':
                case CON_KEY_ESC:
                case CON_KEY_CTRL_C:
                    rc = PLAY_QUIT;
                    goto out;

                case 'n': case 'N':
                    rc = PLAY_SKIP;
                    goto out;

                case ' ': case 'p': case 'P':
                    player_toggle_pause(p);
                    break;

                case CON_KEY_LEFT:  player_seek_by(p, -SEEK_SMALL); break;
                case CON_KEY_RIGHT: player_seek_by(p, +SEEK_SMALL); break;
                case CON_KEY_PGUP:  player_seek_by(p, -SEEK_BIG);   break;
                case CON_KEY_PGDN:  player_seek_by(p, +SEEK_BIG);   break;

                case CON_KEY_UP:    player_volume(p, +VOLUME_STEP); break;
                case CON_KEY_DOWN:  player_volume(p, -VOLUME_STEP); break;

                case CON_KEY_HOME:  player_seek_to(p, 0);           break;
                case CON_KEY_END:   player_seek_to(p, UINT64_MAX);  break;

                case 'r': case 'R':
                    player_seek_to(p, 0);
                    break;

                default:
                    break;
                }
            }
            ui_draw(p, &ui);
        } else {
            PlayerSnap s;
            player_snapshot(p, &s);
            if (s.eof && !s.paused && s.queued == 0)
                break;
        }

        Sleep(interactive ? UI_REFRESH_MS : 100);
    }

out:
    if (interactive) {
        con_gotoxy(0, ui.base_y + UI_LINES);
        con_show_cursor(1);
        con_reset_color();
        printf("\n");
        fflush(stdout);
    }
    return rc;
}

static int play_file(const char *path, const Options *o)
{
    char      err[256], aerr[256];
    PcmSource *src;
    Player    p;
    int       rc;

    src = source_open(path, &o->sopts, err, sizeof(err));
    if (!src) {
        pcm_log_error("%s", err);
        return PLAY_ERROR;
    }

    memset(&p, 0, sizeof(p));
    p.src         = src;
    p.fmt         = *source_format(src);
    p.frame_size  = audio_frame_size(&p.fmt);
    p.volume      = o->volume;
    p.frames_filled = source_frame_pos(src);
    InitializeSRWLock(&p.lock);

    p.aout = aout_open(&p.fmt, 600, player_fill, &p, aerr, sizeof(aerr));
    if (!p.aout) {
        pcm_log_error("%s: %s", path, aerr);
        source_close(src);
        return PLAY_ERROR;
    }

    p.scratch = (uint8_t *)malloc((size_t)aout_buffer_frames(p.aout) *
                                  p.frame_size);
    if (!p.scratch) {
        pcm_log_error("%s: out of memory", path);
        aout_close(p.aout);
        source_close(src);
        return PLAY_ERROR;
    }

    if (o->quiet)
        pcm_log_info("playing %s", path);

    aout_start(p.aout);
    rc = run_loop(&p, o);

    aout_close(p.aout);
    free(p.scratch);
    source_close(src);
    return rc;
}

/* ------------------------------------------------------------------ */
/* command line                                                        */
/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
"pcmplay " PCM_VERSION " - lightweight WAV / PCM player for Windows x64\n"
"\n"
"usage: pcmplay [options] <file> [more files...]\n"
"\n"
"input options (for headerless raw PCM files):\n"
"  -r, --raw              treat input as headerless raw PCM\n"
"  --rate <hz>            sample rate of raw input     (default 44100)\n"
"  --channels <n>         channel count of raw input   (default 2)\n"
"  --format <fmt>         sample format of raw input   (default s16)\n"
"                         u8, s16, s24, s32, f32\n"
"\n"
"playback options:\n"
"  -s, --start <time>     start position (12.5, 1:30, 1h2m3s, 500ms)\n"
"  -t, --duration <time>  stop after that much audio\n"
"  -v, --volume <pct>     initial volume in percent, 0-200 (default 100)\n"
"  -q, --quiet            no interactive UI, just play to the end\n"
"\n"
"misc:\n"
"      --debug            verbose logging\n"
"  -h, --help             show this help\n"
"  -V, --version          show version\n"
"\n"
"keys (interactive mode):\n"
"  space        pause / resume         left/right   seek -/+ 5s\n"
"  pgup/pgdn    seek -/+ 30s           up/down      volume -/+ 5%%\n"
"  home/end     jump to start/end      r            restart file\n"
"  n            next file              q or esc     quit\n");
}

static int need_value(int i, int argc, char **argv, const char **out)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "error: %s requires a value\n", argv[i]);
        return -1;
    }
    *out = argv[++i];
    return 0;
}

int main(int argc, char **argv)
{
    Options       o;
    const char   *files[256];
    const char   *val;
    int           nfiles = 0;
    int           i, rc = 0;
    unsigned long tmp;

    memset(&o, 0, sizeof(o));
    o.sopts.raw_rate     = 44100;
    o.sopts.raw_channels = 2;
    o.sopts.raw_format   = SAMPLE_S16;
    o.volume             = 1.0f;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (arg[0] != '-' || arg[1] == '\0') {           /* plain file name */
            if (nfiles < (int)(sizeof(files) / sizeof(files[0])))
                files[nfiles++] = arg;
            else {
                fprintf(stderr, "error: too many input files\n");
                return 2;
            }
            continue;
        }

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
            printf("pcmplay %s\n", PCM_VERSION);
            return 0;
        }
        if (!strcmp(arg, "-r") || !strcmp(arg, "--raw")) {
            o.sopts.raw = 1;
        } else if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
            o.quiet = 1;
        } else if (!strcmp(arg, "--debug")) {
            o.debug = 1;
        } else if (!strcmp(arg, "--rate")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            tmp = strtoul(val, NULL, 10);
            if (tmp < 1000 || tmp > 768000) {
                fprintf(stderr, "error: bad sample rate '%s'\n", val);
                return 2;
            }
            o.sopts.raw_rate = (uint32_t)tmp;
            ++i;
        } else if (!strcmp(arg, "--channels")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            tmp = strtoul(val, NULL, 10);
            if (tmp < 1 || tmp > 16) {
                fprintf(stderr, "error: bad channel count '%s'\n", val);
                return 2;
            }
            o.sopts.raw_channels = (uint16_t)tmp;
            ++i;
        } else if (!strcmp(arg, "--format")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            if (audio_format_parse(val, &o.sopts.raw_format) != 0) {
                fprintf(stderr, "error: unknown sample format '%s'\n", val);
                return 2;
            }
            ++i;
        } else if (!strcmp(arg, "-s") || !strcmp(arg, "--start")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            if (pcm_parse_time(val, &o.sopts.start_sec) != 0) {
                fprintf(stderr, "error: bad time '%s'\n", val);
                return 2;
            }
            ++i;
        } else if (!strcmp(arg, "-t") || !strcmp(arg, "--duration")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            if (pcm_parse_time(val, &o.sopts.duration_sec) != 0 ||
                o.sopts.duration_sec <= 0.0) {
                fprintf(stderr, "error: bad duration '%s'\n", val);
                return 2;
            }
            ++i;
        } else if (!strcmp(arg, "-v") || !strcmp(arg, "--volume")) {
            if (need_value(i, argc, argv, &val) != 0) return 2;
            tmp = strtoul(val, NULL, 10);
            if (tmp > 200) {
                fprintf(stderr, "error: volume must be 0-200\n");
                return 2;
            }
            o.volume = (float)tmp / 100.0f;
            ++i;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n\n", arg);
            usage(stderr);
            return 2;
        }
    }

    if (nfiles == 0) {
        usage(stderr);
        return 2;
    }

    pcm_set_log_level(o.debug ? LOG_LEVEL_DEBUG
                              : (o.quiet ? LOG_LEVEL_QUIET : LOG_LEVEL_INFO));

    con_init();

    for (i = 0; i < nfiles; ++i) {
        int r = play_file(files[i], &o);
        if (r == PLAY_QUIT)
            break;
        if (r == PLAY_ERROR)
            rc = 1;
    }

    con_reset_color();
    return rc;
}
