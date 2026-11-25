# Architecture notes

How the port fits together, and why it looks the way it does. Everything here was
learned on hardware; the failures are documented alongside the fixes because they are
the expensive part.

## Layout

```
overlay/xbmc/platform/switch/     the platform layer (window, input, audio, storage)
overlay/xbmc/windowing/switch/    WinSystem + GLES context
overlay/cmake/                    toolchain and platform cmake
overlay/xbmc/...                  targeted patches to core Kodi files
```

Platform classes Kodi expects:

| Kodi interface | Switch implementation |
|---|---|
| `CWinSystemBase` | `CWinSystemSwitch` + `CWinSystemSwitchGLESContext` |
| `IInputHandler` | `CWinEventsSwitch` (pad buttons, analog stick mapped to direction keys, native touch gestures) |
| `IAESink` | `CAESinkSwitch` (audout) |
| Storage | `CSwitchStorageProvider` |
| System info | `CSwitchSystemInfo` |

## Binary add-ons: the dlopen problem

Horizon has no dynamic loader for NRO code, and devkitPro's `dlfcn.h` ships
**`static inline` stubs** for `dlopen`/`dlsym`/`dlclose`/`dlerror` that return NULL.
Any translation unit that includes `<dlfcn.h>` silently binds to those stubs and never
reaches a real implementation — a genuinely nasty trap.

The port therefore:

1. names its own entry points `switch_dlopen`, `switch_dlsym`, `switch_dlclose`,
   `switch_dlerror` and redirects call sites in `SoLoader.cpp`;
2. builds binary add-ons as **static libraries**, links them into `kodi.bin`, and
   serves `ADDON_Create`, `ADDON_GetTypeVersion`, `ADDON_GetTypeMinVersion` from the
   shim, matching on the add-on path;
3. writes the add-on's `addon.xml` to declare `library_switch="<name>.a"` and creates a
   **zero-byte file with that name** in the add-on folder, because Kodi checks that the
   declared library file exists *before* it tries to load anything.

`inputstream.adaptive` is built this way and works (HLS with HTTPS).

## Video decode

### NVDEC (HEVC only)

`AV_HWDEVICE_TYPE_NVTEGRA` is reachable from an hbmenu application — no special
privileges. The flow:

1. `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_NVTEGRA)` once per decoder;
   attach `avctx->hw_device_ctx` before `avcodec_open2`.
2. `get_format` returns `AV_PIX_FMT_NVTEGRA` when offered, and records
   `m_nvtegraHw = true`.
3. Each frame is copied out with `av_hwframe_transfer_data`. The destination format must
   come from `((AVHWFramesContext*)frame->hw_frames_ctx->data)->sw_format`: **NV12 for
   8-bit, P010 for Main 10**. Hardcoding NV12 fails silently on 10-bit — audio plays,
   video never appears.
4. `av_frame_copy_props` afterwards, or timestamps (and therefore A/V sync) are lost.
5. P010 is converted down to NV12 with one `sws_scale` pass, because the GLES renderer
   has no 10-bit path.
6. If `avcodec_open2` fails with the device attached, drop `hw_device_ctx` and retry in
   software.

Two hard-won details:

- **`avctx->pix_fmt` must not stay `AV_PIX_FMT_NVTEGRA`.** Kodi's software decode path
  decides whether it needs a filter graph from that field, and builds the graph's buffer
  source from it too. A hardware format there makes every frame try to open a
  hardware-format graph, which always fails, is retried next frame, and eventually
  terminates the process. Measured: 511 failed opens in 31 s. After the transfer, set
  `pix_fmt` and `sw_pix_fmt` to `AV_PIX_FMT_NV12`.
- Because the frames are software again, Kodi's hardware flag would read false and the
  playback guard would refuse 1080p HEVC. `m_nvtegraHw` is therefore fed explicitly into
  `ProcessInfo`/`DataCache` from `UpdateName()`.

### Playback guard

`SwitchPlaybackGuard` in `Application.cpp` refuses combinations this console cannot
decode in real time, instead of letting the player fall apart. Its starvation heuristic
must only apply to **software** decoding: a hardware decoder legitimately keeps the video
queue at 0–1 while presenting at full rate, and applying the rule there stopped healthy
playback ~90 s in.

## Networking

Horizon's bsd service owns a **small fixed buffer pool** for the whole process.
Measured on this console: with 128 KB defaults and `sb_efficiency=4`, the process could
open roughly **2–5 sockets** at idle. That pool cannot be enlarged from
`SocketInitConfig`; the numbers you pass determine both the per-socket reservation and
the size of the pool itself (shrinking them made it *worse* — 32 KB defaults produced
zero usable sockets).

Consequences and choices:

- every curl handle sets `SO_RCVBUF` to **64 KB** (a 256 KB window measured faster but
  reserved so much pool that a second playback could not connect);
- `CURLOPT_FORBID_REUSE` (and `CURLOPT_MAXCONNECTS 1`) so closed transfers release their
  sockets instead of parking them in libcurl's idle cache;
- add-ons that keep a Python `requests` session alive must send
  `Connection: close`, or they exhaust the pool mid-flow (`ENOBUFS`, errno 105);
- background services are the biggest idle consumers — check what is enabled before
  blaming an add-on.

`tls_libnx.c` (ffmpeg's TLS) needs `sslInitialize(numSessions)`; Kodi's own HTTPS goes
through OpenSSL/curl and does not.

## Threads and teardown

Kodi's `CThread` is a `private` base of `CJobManager::CJobWorker`. An exception thrown
from `CThread::~CThread` (which is implicitly `noexcept`) becomes `std::unexpected` →
`std::terminate`, and the whole application dies mid-playback. Measured chain:

```
CJobWorker::~CJobWorker -> CThread::~CThread -> std::unexpected -> __terminate
```

The throw comes from inside `StopThread()` (event/condition-variable wait), not from
`detach()`, so the destructor body is wrapped and the exception text is traced. Two
supporting fixes: `Create()` catches thread-creation failure (libnx reports it as
`std::system_error`) and sets its start event before returning, and `CJobManager` refuses
to keep a worker whose thread never started — otherwise it counts a phantom worker as
free forever and stops handing out jobs.

**Resolved (2026-11-13):** the throw was never the condition-variable wait. Read out of
the linked binary, `pthread_detach()` on this toolchain is a stub returning `ENOSYS` (88)
without doing anything, while `pthread_join()` is real (it calls `__syscall_thread_join`)
and `pthread_cond_timedwait()` is implemented. Auto-delete workers (`CJobWorker` uses
`Create(true)`) run `~CThread` on their own thread, cannot join themselves, and therefore
reached `detach()` — which threw every time a job finished. The destructor no longer
detaches on Switch: it parks the `std::thread*` and the next `Create()` on another thread
joins and deletes it, which also reclaims the handle. Measured: zero escapes across a
playback that previously produced one within a minute.

## Files, audio, screenshots

- **FAT quirks**: a file opened for writing cannot be opened by anyone else (`EIO`), so
  the diagnostic trace file is written open/append/close per line to stay readable while
  the process runs.
- **Audio**: `CAESinkSwitch` drives `audout` directly, 48 kHz stereo, five 1024-frame
  buffers.
- **Screenshots**: Kodi 22 routes screenshots through `CCaptureService`, and only the
  DRM-PRIME renderer provides a capture provider — so `TakeScreenshot` silently wrote
  nothing. The Switch path does a synchronous `glReadPixels` on the default framebuffer,
  flips vertically, and goes through `CPicture::CreateThumbnailFromSurface()` (which
  needs an image encoder in ffmpeg — see `docs/BUILDING.md`).

## Crash handling

`spdlog` keeps up to a couple of seconds unflushed, and Atmosphère's reports have no
symbols for an NRO loaded by hbloader. The port therefore keeps its own trace file,
`boot.log`, written directly through libnx's devoptab, and installs `std::set_terminate`
plus SIGABRT/SIGSEGV handlers that record the reason synchronously (never rethrowing
inside the handler — that terminates again).

`aarch64-none-elf-addr2line` against `build-kodi/kodi.bin` resolves the PC and return
addresses from an Atmosphère report, which is how the thread-teardown bug above was
found.
