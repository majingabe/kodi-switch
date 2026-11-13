# Kodi for Nintendo Switch

A working native port of **Kodi 22 "Piers"** (rc1, `28ea2ea`) to homebrew-capable
Nintendo Switch consoles running Atmosphère.

This is not an emulator, a launcher, or a wrapper around an external player: it is
Kodi's own codebase, compiled for `aarch64-none-elf`, running as a `.nro` from the
HOME menu with the full GUI, its own GPU renderer, its own audio sink, its own input
layer, and the real CPython interpreter — so normal Kodi add-ons and scrapers run.

> Status: **in development**. Everything below marked *Working* has been measured on
> hardware (see `docs/PORT-STATUS.md` for the raw numbers). Anything marked *Not
> working* is a known limitation, not a claim it will never work.

---

## What it does

**Core**
- Full Kodi GUI on the Switch's GLES2 context, 720p (handheld and docked)
- Hardware video decode (**NVDEC**) for HEVC, including **10-bit (Main 10)**
- Software decode for everything else (H.264 and friends)
- Audio through a native `CAESinkSwitch` (audout), stereo, 48 kHz
- Input: both Joy-Cons / Pro Controller, both analog sticks, and touch
- CPython 3.14 embedded — standard Python add-ons install and run
- `inputstream.adaptive` **built in and statically linked** (HLS with HTTPS)
- JSON-RPC on port 9090 for remote control and automation
- Screenshots that actually work (framebuffer capture, not the DRM path)
- SD card, FAT/exFAT storage, and network filesystems via Kodi's own VFS

**Networking**
- HTTPS with certificate validation (OpenSSL + CA bundle)
- Tuned socket pool: usable throughput of several MB/s on LAN
- Streaming from ordinary HTTP servers with seek support (Range requests)

**Media**
- Local playback from SD at 1080p H.264 real time
- 1080p 10-bit HEVC over the network at ~1.00x real time, verified for 10 minutes
- Add-on streaming (embed-style sources) at ~1.01x
- Seeking, subtitles, audio track selection, OSD

## What it deliberately does not do (yet)

- **Binary add-ons** cannot be loaded: this platform has no `dlopen` for NROs. Kodi's
  add-on system is worked around with a shim (see `docs/ARCHITECTURE.md`), and
  `inputstream.adaptive` is linked in directly. PVR clients (`pvr.iptvsimple`),
  visualisations, and other binary add-ons are therefore unavailable until each is
  ported the same way.
- **No CD/DVD**, no optical drive support.
- **Exit is slow** (~30 s) and lands on the HOME menu, because hbmenu's applet closes
  when a full-memory application starts.
- **No hardware H.264 decode** — only HEVC goes through NVDEC. H.264 uses the proven
  software path to avoid regressing working playback.
- **Clock shows UTC** in Kodi's own labels; Python and the OS see local time.
- Only one launch path is supported: full-memory hbmenu (hold `R` on a title). Album
  applet mode does not have the memory for this.

## Requirements

- A Switch with **Atmosphère** (developed against 1.12.x / firmware 23.0.1) and hbmenu
- A way to send the NRO: `nxlink` over the network, or a file copy to `sdmc:/switch/`
- About 1 GB free on the SD card for Kodi's data directory (add-ons, thumbnails, DBs)
- Recommended: an **NSP forwarder** so Kodi launches from the HOME menu with full memory

## Quick start

Build instructions, toolchain setup and the ffmpeg rebuild are in
[`docs/BUILDING.md`](docs/BUILDING.md). Short version:

```sh
# 1. Kodi source at ../kodi (see docs/BUILDING.md for the exact checkout)
# 2. Apply the Switch overlay
./scripts/wsl-apply-overlay.sh
# 3. Build and package
./scripts/wsl-build-kodi.sh
./scripts/wsl-package-kodi.sh      # produces package/kodi.nro
# 4. Deploy
./scripts/wsl-nxlink-run.sh <switch-ip>
```

The runtime tree (`addons/`, `system/`, `python/`, `media/`) goes to
`sdmc:/switch/kodi/`; the NRO can live anywhere, most people use
`sdmc:/switch/kodi.nro`.

## How it is built

Kodi's own CMake with a `switch` toolchain, plus a set of platform files under
`overlay/xbmc/platform/switch/` (window system, input, audio, storage, system info)
and targeted patches to core files that assume a desktop OS.

The interesting problems and their solutions are written up in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md):

- the `dlopen` shim for statically linked binary add-ons
- the NVDEC path (hardware HEVC → NV12, including a P010 downconvert for 10-bit)
- the socket configuration and what the bsd pool actually allows
- crash handling that survives the death of the process
- why a `noexcept` destructor can kill the whole application, and the fix

## Roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md). In short: faster exit, PVR (live TV) by
statically linking `pvr.iptvsimple`, a proper pool for frame buffers, and — as a
long-term research project — in-process BitTorrent streaming.

## Credits

Ported and maintained by **MajinGabe**.

- **Team Kodi / XBMC Foundation** — Kodi itself, and the platform abstractions this
  port plugs into.
- **devkitPro / devkitA64 / libnx** — the Switch toolchain and system library.
- **switchbrew** — documentation of Horizon OS that made the platform layer possible.
- **CastagnaIT and contributors** — `inputstream.adaptive`, linked in statically here.
- The homebrew community for hbmenu, nxlink, and the endless field notes.

## Licence

Kodi is **GPL-2.0-or-later**; this port's changes are released under the same terms.
See [`LICENSE.md`](LICENSE.md) and [`LICENSES/`](LICENSES/). Binary components linked
into the NRO keep their own licences (see `docs/BUILDING.md`).

This project is not affiliated with Nintendo. It contains no Nintendo code, keys or
content, and no add-ons. Add-ons you install are your responsibility.
