# Handover — state of the Kodi Switch port

Written 2026-11-14 at the end of a long session. Read this first; then
`docs/PORT-STATUS.md` for the detailed history, newest entries at the bottom.

> **Before changing anything on the console, work through `CHECKLIST.md`.**
> It is mandatory, and every item in it exists because skipping it already cost us
> a session — including the `Addons33.db` rebuild that silently disabled 106 of 117
> add-ons and was reported as "it broke my add-on".
>
> **This file is superseded by `MIGRATION.md` (2026-11-16)** — that is the self-contained
> document to hand to a fresh session. Keep this one for the per-area fix detail.

## What is deployed on the console right now

A local build (not a release) containing everything below. It is newer than
`v22.0rc3` and includes the add-ons database repair hook.

**The repo is one commit ahead of the deployed build.** `764e3bf` removed the
per-request `curl open` traces and timestamped the database-repair target. Those are
committed but **not built or deployed** — the next build picks them up.

## Shipped

- `v22.0rc3` — https://github.com/MajinGabe/kodi-switch/releases/tag/v22.0rc3
- Store PR **#1078** points at rc3: 3 commits, 7 files (pkgbuild + icon + 5
  screenshots), package validates with 2,699 manifest entries.

## Fixed today, all verified on hardware by the user

| Area | What was wrong | State |
|---|---|---|
| Thread teardown | `pthread_detach` is a stub returning ENOSYS; auto-delete workers threw from a noexcept dtor | parked + joined by a reaper thread; 0 escapes in 18.7 min |
| Playback guard | refused 1080p HEVC before `avcodec_open2` finished | gated on `GetTime() > 5s`; One Pace MKV plays with NVDEC |
| Clock | port had `mktime()` where `timegm()` belonged, and a hardcoded 0 timezone bias | local time correct |
| System info | `GetMemoryStatus` was a stub; no OS name | real heap (`195MB / 3084MB`), "Atmosphère on Horizon" |
| Screenshots | second `TakeScreenshot` overload used the capture service and wrote nothing | works, 1280x720 PNGs |
| JSON-RPC | accept loop spun ~8,000x/s on ECONNABORTED and leaked a client per failure | backs off, no leak |
| Screen dimming | Horizon auto-sleep timer; app never declared media playback | `appletSetMediaPlaybackState` + `appletSetAutoSleepDisabled` |
| Add-on repository | corrupt `Addons33.db` made every repo list nothing | repair hook at preinit; skins/sounds install and run |

## Verified working on hardware

- playback: play, seek, back out, resume, replay within 5 s
- 1080p 10-bit HEVC with NVDEC; H.264 on the software path
- exit immediate from an NSP forwarder (hbmenu exit is slow — loader teardown)
- Bluetooth audio
- official-repo installs: `skin.confluence` installed and ran; 14 skins and 19 UI
  sound packs available

## Open work, in the order I would take it

1. **Binary add-ons by static linking** — the user's current goal.
   - The shim in `xbmc/platform/switch/PlatformSwitch.cpp` is hardcoded to ONE
     add-on (`ADDON_Create` is a single symbol). Two static add-ons collide.
     Each must be compiled with prefixed symbols (e.g.
     `-DADDON_Create=PVR_ADDON_Create`) and the shim must dispatch on add-on path.
   - Headers: `kodi/xbmc/addons/kodi-dev-kit/include/kodi`
   - Proven precedent: `inputstream.adaptive` (deps built under `isa-src/`)
   - First target: `pvr.iptvsimple` (Live TV, EPG, M3U/XMLTV). Pure C++, modest deps.
   - Packaging per add-on: `library_switch="<name>.a"` in addon.xml **plus a
     zero-byte file of that name**, or Kodi refuses to load it.
2. **SwitchCast / sysDVR** — `/switch/SwitchCast.nro` exists and is built on
   SysDVR (GPL-2.0); sysmodule dir `00FF000053434153` ("SCAS") is in
   `/atmosphere/contents`. Streaming while Kodi runs should be possible; it will
   compete for WiFi bandwidth and sockets.
3. **sys-ftpd** — installed and configured (`user=deepsea`, port 5000) but not
   running. Enabling `flags/boot2` on its sysmodule would give FTP access to the
   SD from the PC, which would remove most of the file-access friction.
4. Battery still reports 0% (`psm` init succeeds, percentage reads 0).
5. Card work: `h2testw`, homebrew inventory, cleanup.

## Traps that cost time — do not rediscover these

- **Port 9090 is raw JSON-RPC TCP**, not HTTP: 4-byte big-endian length prefix,
  bare JSON response. HTTP to it yields a header-less `400 Bad Request`.
- **`boot.log` is append-only** across hundreds of boots and is the only log
  readable while Kodi runs. Every boot writes `kodi-switch build: threads <date>`.
- **FAT**: a file open for writing cannot be opened by anything else (`EIO`), and
  `rename` fails too. This is why the DB repair runs at preinit.
- **The socket pool is ~9 sockets.** A 7 MB skin install exhausts it and takes
  JSON-RPC down until it finishes. Polling RPC every 2 s during playback is load
  a user never applies.
- **The RPC test harness is not a usage simulation.** It has produced at least
  three false bugs. Confirm behaviour by hand before "fixing" anything.
- `kodi_rpc.py` reads `sys.argv[1]` as the host — set `kodi_rpc.IP` explicitly in
  scripts that take their own arguments.
- The probe (`script.pyprobe`, port 9998) is the only way to read/write files on
  the console from the PC without the card. `xbmcvfs.exists()` lies for
  directories; use `os.makedirs`.

## Useful facts

- Console `192.168.1.191`, PC `192.168.1.234`; media server on 8003
- Build: `wsl -d Ubuntu -u kidbu -- bash /mnt/c/dev/kodi-switch/rebuild.sh`
  (syncs overlay → applies → builds → packages)
- Deploy: user presses Y in hbmenu, then `wsl-nxlink-run.sh`
- Screenshots: `shot_clock.py` / `shot_sysinfo.py`; pull from `/Nintendo/Album`
  with `fetch_album.py`
