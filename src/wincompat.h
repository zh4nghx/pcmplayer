/*
 * wincompat.h - Windows API compatibility shim.
 *
 * On MSVC / MinGW-w64 the regular SDK headers are used.  When building
 * with tinycc (whose bundled winapi headers lack WASAPI, COM activation
 * and SRWLOCK declarations) self-contained declarations are provided so
 * the player compiles unchanged.
 */
#ifndef PCM_WINCOMPAT_H
#define PCM_WINCOMPAT_H

#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>

#if defined(__TINYC__)

/* ------------------------------------------------------------------ */
/* tinycc: minimal COM / WASAPI / lock declarations                    */
/* ------------------------------------------------------------------ */

#include <basetyps.h>
#include <guiddef.h>

typedef long long PCM_LONGLONG;
#ifndef REFERENCE_TIME
typedef PCM_LONGLONG REFERENCE_TIME;
#endif

#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
typedef struct tcc_WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;
#endif

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM        1
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 3
#endif

/* --- CoInitializeEx / CoCreateInstance (ole32) --------------------- */

#ifndef COINIT_MULTITHREADED
#define COINIT_MULTITHREADED 0x0
#endif

struct IUnknown;
typedef struct IUnknown *LPUNKNOWN;

HRESULT WINAPI CoInitializeEx(LPVOID pvReserved, DWORD dwCoInit);
void     WINAPI CoUninitialize(void);
HRESULT WINAPI CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter,
                                DWORD dwClsContext, REFIID riid,
                                LPVOID *ppv);

/* --- WASAPI interfaces --------------------------------------------- */

typedef struct IMMDeviceEnumerator IMMDeviceEnumerator;
typedef struct IMMDevice           IMMDevice;
typedef struct IAudioClient        IAudioClient;
typedef struct IAudioRenderClient  IAudioRenderClient;

typedef struct IMMDeviceEnumeratorVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMMDeviceEnumerator *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IMMDeviceEnumerator *);
    ULONG   (STDMETHODCALLTYPE *Release)(IMMDeviceEnumerator *);
    HRESULT (STDMETHODCALLTYPE *EnumAudioEndpoints)(IMMDeviceEnumerator *, int, DWORD, void **);
    HRESULT (STDMETHODCALLTYPE *GetDefaultAudioEndpoint)(IMMDeviceEnumerator *, int, int, IMMDevice **);
    HRESULT (STDMETHODCALLTYPE *GetDevice)(IMMDeviceEnumerator *, LPCWSTR, IMMDevice **);
    HRESULT (STDMETHODCALLTYPE *RegisterEndpointNotificationCallback)(IMMDeviceEnumerator *, void *);
    HRESULT (STDMETHODCALLTYPE *UnregisterEndpointNotificationCallback)(IMMDeviceEnumerator *, void *);
} IMMDeviceEnumeratorVtbl;

struct IMMDeviceEnumerator {
    const IMMDeviceEnumeratorVtbl *lpVtbl;
};

typedef struct IMMDeviceVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IMMDevice *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IMMDevice *);
    ULONG   (STDMETHODCALLTYPE *Release)(IMMDevice *);
    HRESULT (STDMETHODCALLTYPE *Activate)(IMMDevice *, REFIID, DWORD, void *, void **);
    HRESULT (STDMETHODCALLTYPE *OpenPropertyStore)(IMMDevice *, DWORD, void **);
    HRESULT (STDMETHODCALLTYPE *GetId)(IMMDevice *, LPWSTR *);
    HRESULT (STDMETHODCALLTYPE *GetState)(IMMDevice *, DWORD *);
} IMMDeviceVtbl;

struct IMMDevice {
    const IMMDeviceVtbl *lpVtbl;
};

typedef struct IAudioClientVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAudioClient *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IAudioClient *);
    ULONG   (STDMETHODCALLTYPE *Release)(IAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Initialize)(IAudioClient *, int, DWORD,
                                             REFERENCE_TIME, REFERENCE_TIME,
                                             const WAVEFORMATEX *, LPCGUID);
    HRESULT (STDMETHODCALLTYPE *GetBufferSize)(IAudioClient *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetStreamLatency)(IAudioClient *, REFERENCE_TIME *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentPadding)(IAudioClient *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *IsFormatSupported)(IAudioClient *, int, const WAVEFORMATEX *, WAVEFORMATEX **);
    HRESULT (STDMETHODCALLTYPE *GetMixFormat)(IAudioClient *, WAVEFORMATEX **);
    HRESULT (STDMETHODCALLTYPE *GetDevicePeriod)(IAudioClient *, REFERENCE_TIME *, REFERENCE_TIME *);
    HRESULT (STDMETHODCALLTYPE *Start)(IAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Stop)(IAudioClient *);
    HRESULT (STDMETHODCALLTYPE *Reset)(IAudioClient *);
    HRESULT (STDMETHODCALLTYPE *SetEventHandle)(IAudioClient *, HANDLE);
    HRESULT (STDMETHODCALLTYPE *GetService)(IAudioClient *, REFIID, void **);
} IAudioClientVtbl;

struct IAudioClient {
    const IAudioClientVtbl *lpVtbl;
};

typedef struct IAudioRenderClientVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAudioRenderClient *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(IAudioRenderClient *);
    ULONG   (STDMETHODCALLTYPE *Release)(IAudioRenderClient *);
    HRESULT (STDMETHODCALLTYPE *GetBuffer)(IAudioRenderClient *, UINT32, BYTE **);
    HRESULT (STDMETHODCALLTYPE *ReleaseBuffer)(IAudioRenderClient *, UINT32, DWORD);
} IAudioRenderClientVtbl;

struct IAudioRenderClient {
    const IAudioRenderClientVtbl *lpVtbl;
};

/* --- WASAPI constants ----------------------------------------------- */

#define AUDCLNT_SHAREMODE_SHARED 0

#define AUDCLNT_STREAMFLAGS_EVENTCALLBACK         0x00040000u
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM        0x80000000u
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY   0x08000000u

#define PCM_eRender  0
#define PCM_eConsole 0

/* --- interface call macros (MinGW-style names) ---------------------- */

#define IMMDeviceEnumerator_Release(This) \
    ((This)->lpVtbl->Release(This))
#define IMMDeviceEnumerator_GetDefaultAudioEndpoint(This, flow, role, dev) \
    ((This)->lpVtbl->GetDefaultAudioEndpoint((This), (flow), (role), (dev)))

#define IMMDevice_Activate(This, iid, ctx, params, out) \
    ((This)->lpVtbl->Activate((This), (iid), (ctx), (params), (out)))
#define IMMDevice_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IAudioClient_Initialize(This, share, flags, dur, period, fmt, guid) \
    ((This)->lpVtbl->Initialize((This), (share), (flags), (dur), (period), (fmt), (guid)))
#define IAudioClient_SetEventHandle(This, event) \
    ((This)->lpVtbl->SetEventHandle((This), (event)))
#define IAudioClient_GetBufferSize(This, frames) \
    ((This)->lpVtbl->GetBufferSize((This), (frames)))
#define IAudioClient_GetService(This, iid, out) \
    ((This)->lpVtbl->GetService((This), (iid), (out)))
#define IAudioClient_GetCurrentPadding(This, padding) \
    ((This)->lpVtbl->GetCurrentPadding((This), (padding)))
#define IAudioClient_Start(This) \
    ((This)->lpVtbl->Start(This))
#define IAudioClient_Stop(This) \
    ((This)->lpVtbl->Stop(This))
#define IAudioClient_Reset(This) \
    ((This)->lpVtbl->Reset(This))
#define IAudioClient_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IAudioRenderClient_GetBuffer(This, frames, data) \
    ((This)->lpVtbl->GetBuffer((This), (frames), (data)))
#define IAudioRenderClient_ReleaseBuffer(This, frames, flags) \
    ((This)->lpVtbl->ReleaseBuffer((This), (frames), (flags)))
#define IAudioRenderClient_Release(This) \
    ((This)->lpVtbl->Release(This))

/* --- GUIDs ----------------------------------------------------------- */

/* Same values as CLSID_MMDeviceEnumerator / IID_* from the Windows SDK. */
static const GUID PCM_CLSID_MMDeviceEnumerator =
    {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID PCM_IID_IMMDeviceEnumerator =
    {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID PCM_IID_IAudioClient =
    {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID PCM_IID_IAudioRenderClient =
    {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};

#define PCM_HAS_WASAPI_HEADERS 0
#define PCM_HAS_ERROR_CODES    0

/* --- SRWLOCK fallback via CRITICAL_SECTION -------------------------- */

typedef CRITICAL_SECTION SRWLOCK;

static void tcc_InitializeSRWLock(SRWLOCK *lock) { InitializeCriticalSection(lock); }
static void tcc_AcquireSRWLockExclusive(SRWLOCK *lock) { EnterCriticalSection(lock); }
static void tcc_ReleaseSRWLockExclusive(SRWLOCK *lock) { LeaveCriticalSection(lock); }
static void tcc_AcquireSRWLockShared(SRWLOCK *lock) { EnterCriticalSection(lock); }
static void tcc_ReleaseSRWLockShared(SRWLOCK *lock) { LeaveCriticalSection(lock); }

#define InitializeSRWLock        tcc_InitializeSRWLock
#define AcquireSRWLockExclusive  tcc_AcquireSRWLockExclusive
#define ReleaseSRWLockExclusive  tcc_ReleaseSRWLockExclusive
#define AcquireSRWLockShared     tcc_AcquireSRWLockShared
#define ReleaseSRWLockShared     tcc_ReleaseSRWLockShared

#else /* !__TINYC__ */

/* ------------------------------------------------------------------ */
/* MSVC / MinGW-w64: use the real SDK headers                          */
/* ------------------------------------------------------------------ */

#include <initguid.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#define PCM_CLSID_MMDeviceEnumerator  CLSID_MMDeviceEnumerator
#define PCM_IID_IMMDeviceEnumerator   IID_IMMDeviceEnumerator
#define PCM_IID_IAudioClient          IID_IAudioClient
#define PCM_IID_IAudioRenderClient    IID_IAudioRenderClient
#define PCM_eRender                   eRender
#define PCM_eConsole                  eConsole

#define PCM_HAS_WASAPI_HEADERS 1
#define PCM_HAS_ERROR_CODES    1

#endif /* __TINYC__ */

#endif /* PCM_WINCOMPAT_H */
