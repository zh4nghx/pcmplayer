/*
 * out.c - WASAPI shared-mode audio renderer.
 *
 * The stream is initialized on a dedicated render thread with
 * AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
 * so the Windows audio engine performs any required sample rate / channel
 * conversion; we simply push frames in the source format via the fill
 * callback.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wincompat.h"
#include "out.h"

#ifndef CLSCTX_ALL
#define CLSCTX_ALL 0x17
#endif

struct AudioOut {
    /* immutable after aout_open() */
    AudioFormat    fmt;
    WAVEFORMATEX   wfx;
    AoutFillFn     fill;
    void          *user;
    REFERENCE_TIME buffer_duration;

    HANDLE thread;
    HANDLE wake;        /* auto-reset: wakes the render loop          */
    HANDLE init_done;   /* signaled once initialization has finished  */
    HANDLE dead;        /* signaled when the render thread is gone    */
    HANDLE flush_done;  /* signaled once a flush has been processed   */

    /* render thread only */
    IAudioClient       *client;
    IAudioRenderClient *render;

    /* written by the render thread, read after init_done/dead */
    HRESULT init_hr;
    char    init_err[160];
    UINT32  buffer_frames;

    /* shared state (protected by lock or interlocked) */
    volatile LONG running;        /* 1 = render loop alive               */
    volatile LONG start_request;  /* 1 = start the stream                */
    volatile LONG flush_request;  /* 1 = drop queued audio               */
    int     playing;        /* render thread only                  */
    UINT32  last_padding;   /* render thread writes, others read   */
    SRWLOCK lock;           /* serializes render loop vs. flush    */
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *hr_string(HRESULT hr)
{
#if PCM_HAS_ERROR_CODES
    switch ((DWORD)hr) {
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:  return "failed to create the audio endpoint";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:     return "the Windows audio service is not running";
    case AUDCLNT_E_DEVICE_INVALIDATED:      return "the audio device was invalidated";
    case AUDCLNT_E_NOT_SUPPORTED:           return "the requested stream format is not supported";
    case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED: return "the buffer size is not aligned";
    case AUDCLNT_E_OUT_OF_ORDER:            return "audio client calls were out of order";
    case E_POINTER:                         return "internal error (null pointer)";
    case E_OUTOFMEMORY:                     return "out of memory";
    default:                                return NULL;
    }
#else
    (void)hr;
    if (hr == E_POINTER)   return "internal error (null pointer)";
    if (hr == E_OUTOFMEMORY) return "out of memory";
    return NULL;
#endif
}

static void report_init_error(AudioOut *a, HRESULT hr, const char *stage)
{
    const char *msg = hr_string(hr);
    _snprintf(a->init_err, sizeof(a->init_err),
              "WASAPI %s failed%s%s (0x%08lX)",
              stage,
              msg ? ": " : "", msg ? msg : "",
              (unsigned long)hr);
    a->init_err[sizeof(a->init_err) - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* render thread                                                       */
/* ------------------------------------------------------------------ */

static DWORD WINAPI render_thread(LPVOID arg)
{
    AudioOut            *a = (AudioOut *)arg;
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice           *device = NULL;
    int                  com_init = 0;
    HRESULT              hr;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
        com_init = 1;
    else
        goto init_fail;

    hr = CoCreateInstance(&PCM_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &PCM_IID_IMMDeviceEnumerator, (void **)&enumerator);
    if (FAILED(hr))
        goto init_fail;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, PCM_eRender,
                                                     PCM_eConsole, &device);
    if (FAILED(hr)) {
        report_init_error(a, hr, "no default output device");
        goto init_fail;
    }

    hr = IMMDevice_Activate(device, &PCM_IID_IAudioClient, CLSCTX_ALL, NULL,
                            (void **)&a->client);
    if (FAILED(hr)) {
        report_init_error(a, hr, "device activation");
        goto init_fail;
    }

    hr = IAudioClient_Initialize(a->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                 AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                 AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                 a->buffer_duration, 0, &a->wfx, NULL);
    if (FAILED(hr)) {
        report_init_error(a, hr, "stream initialization");
        goto init_fail;
    }

    hr = IAudioClient_SetEventHandle(a->client, a->wake);
    if (FAILED(hr)) {
        report_init_error(a, hr, "SetEventHandle");
        goto init_fail;
    }

    hr = IAudioClient_GetBufferSize(a->client, &a->buffer_frames);
    if (FAILED(hr)) {
        report_init_error(a, hr, "GetBufferSize");
        goto init_fail;
    }

    hr = IAudioClient_GetService(a->client, &PCM_IID_IAudioRenderClient,
                                 (void **)&a->render);
    if (FAILED(hr)) {
        report_init_error(a, hr, "GetService(IAudioRenderClient)");
        goto init_fail;
    }

init_fail:
    if (FAILED(hr) && !a->init_err[0])
        report_init_error(a, hr, "initialization");

    a->init_hr = hr;
    SetEvent(a->init_done);

    if (device)
        IMMDevice_Release(device);
    if (enumerator)
        IMMDeviceEnumerator_Release(enumerator);

    if (FAILED(hr)) {
        if (a->render) { IAudioRenderClient_Release(a->render); a->render = NULL; }
        if (a->client) { IAudioClient_Release(a->client); a->client = NULL; }
        if (com_init)
            CoUninitialize();
        SetEvent(a->dead);
        return 0;
    }

    while (a->running) {
        WaitForSingleObject(a->wake, 100);
        if (!a->running)
            break;

        AcquireSRWLockExclusive(&a->lock);

        if (a->start_request) {
            a->start_request = 0;
            a->playing = 1;
            IAudioClient_Start(a->client);
        }

        if (a->flush_request) {
            int was_playing = a->playing;
            IAudioClient_Stop(a->client);
            IAudioClient_Reset(a->client);
            a->last_padding = 0;
            if (was_playing)
                IAudioClient_Start(a->client);
            a->flush_request = 0;
            SetEvent(a->flush_done);
        }

        if (a->playing) {
            UINT32 padding = 0;

            if (SUCCEEDED(IAudioClient_GetCurrentPadding(a->client, &padding))) {
                a->last_padding = padding;

                if (padding < a->buffer_frames) {
                    UINT32 frames = a->buffer_frames - padding;
                    BYTE  *dst = NULL;

                    if (SUCCEEDED(IAudioRenderClient_GetBuffer(a->render,
                                                               frames, &dst)) &&
                        dst) {
                        a->fill(a->user, dst, frames);
                        IAudioRenderClient_ReleaseBuffer(a->render, frames, 0);
                    }
                }
            }
        }

        ReleaseSRWLockExclusive(&a->lock);
    }

    IAudioClient_Stop(a->client);
    if (a->render) { IAudioRenderClient_Release(a->render); a->render = NULL; }
    if (a->client) { IAudioClient_Release(a->client); a->client = NULL; }
    if (com_init)
        CoUninitialize();
    SetEvent(a->dead);
    return 0;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

AudioOut *aout_open(const AudioFormat *fmt, uint32_t buffer_ms,
                    AoutFillFn fill, void *user,
                    char *err, size_t errlen)
{
    AudioOut *a;
    size_t    bps;

    if (err && errlen)
        err[0] = '\0';

    if (!fmt || !fill) {
        if (err) snprintf(err, errlen, "invalid audio output arguments");
        return NULL;
    }
    bps = (size_t)audio_bytes_per_sample(fmt->format);
    if (bps == 0 || fmt->channels == 0 || fmt->sample_rate == 0) {
        if (err) snprintf(err, errlen, "invalid audio format");
        return NULL;
    }

    if (buffer_ms == 0) buffer_ms = 600;
    if (buffer_ms < 100) buffer_ms = 100;
    if (buffer_ms > 2000) buffer_ms = 2000;

    a = (AudioOut *)calloc(1, sizeof(*a));
    if (!a) {
        if (err) snprintf(err, errlen, "out of memory");
        return NULL;
    }

    a->fmt             = *fmt;
    a->fill            = fill;
    a->user            = user;
    a->buffer_duration = (REFERENCE_TIME)buffer_ms * 10000LL;

    a->wfx.wFormatTag      = (fmt->format == SAMPLE_F32)
                                 ? (WORD)WAVE_FORMAT_IEEE_FLOAT
                                 : (WORD)WAVE_FORMAT_PCM;
    a->wfx.nChannels       = fmt->channels;
    a->wfx.nSamplesPerSec  = fmt->sample_rate;
    a->wfx.wBitsPerSample  = (WORD)(bps * 8);
    a->wfx.nBlockAlign     = (WORD)(fmt->channels * bps);
    a->wfx.nAvgBytesPerSec = a->wfx.nSamplesPerSec * a->wfx.nBlockAlign;
    a->wfx.cbSize          = 0;

    a->wake       = CreateEventW(NULL, FALSE, FALSE, NULL);
    a->init_done  = CreateEventW(NULL, TRUE,  FALSE, NULL);
    a->dead       = CreateEventW(NULL, TRUE,  FALSE, NULL);
    a->flush_done = CreateEventW(NULL, TRUE,  FALSE, NULL);
    if (!a->wake || !a->init_done || !a->dead || !a->flush_done) {
        if (err) snprintf(err, errlen, "CreateEvent failed");
        aout_close(a);
        return NULL;
    }

    InitializeSRWLock(&a->lock);
    a->running = 1;

    a->thread = CreateThread(NULL, 0, render_thread, a, 0, NULL);
    if (!a->thread) {
        if (err) snprintf(err, errlen, "CreateThread failed");
        aout_close(a);
        return NULL;
    }

    if (WaitForSingleObject(a->init_done, 15000) != WAIT_OBJECT_0 ||
        FAILED(a->init_hr)) {
        if (err && errlen) {
            snprintf(err, errlen, "%s",
                     a->init_err[0] ? a->init_err
                                    : "audio device initialization timed out");
        }
        aout_close(a);
        return NULL;
    }

    return a;
}

int aout_start(AudioOut *a)
{
    if (!a || !a->thread)
        return -1;
    InterlockedExchange(&a->start_request, 1);
    SetEvent(a->wake);
    return 0;
}

void aout_flush(AudioOut *a)
{
    if (!a || !a->thread)
        return;
    ResetEvent(a->flush_done);
    InterlockedExchange(&a->flush_request, 1);
    SetEvent(a->wake);
    WaitForSingleObject(a->flush_done, 200);
}

uint32_t aout_buffer_frames(const AudioOut *a)
{
    return a ? a->buffer_frames : 0;
}

uint32_t aout_queued_frames(const AudioOut *a)
{
    return a ? a->last_padding : 0;
}

void aout_close(AudioOut *a)
{
    if (!a)
        return;

    if (a->thread) {
        InterlockedExchange(&a->running, 0);
        SetEvent(a->wake);
        if (WaitForSingleObject(a->dead, 5000) == WAIT_OBJECT_0) {
            CloseHandle(a->thread);
            a->thread = NULL;
        }
    }

    if (a->wake)       CloseHandle(a->wake);
    if (a->init_done)  CloseHandle(a->init_done);
    if (a->dead)       CloseHandle(a->dead);
    if (a->flush_done) CloseHandle(a->flush_done);
    free(a);
}
