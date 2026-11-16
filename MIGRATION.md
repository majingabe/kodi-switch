# MIGRATION — Kodi 22 ("Piers") on Nintendo Switch / Horizon OS

**Written 2026-11-16.** Hand this file to a fresh chat. It is self-contained: read it
top to bottom and you can pick up work without re-deriving anything.

Repo: https://github.com/majingabe/kodi-switch (public, `main`, clean tree)
Source on disk: `C:\dev\kodi-switch-source`
Build scripts: `C:\dev\kodi-switch`
Full history: `docs/PORT-STATUS.md` (newest at bottom, ~135 KB)
**Mandatory process doc: `CHECKLIST.md`** — work through it before touching the console.

---

## 1. What this project is

A working port of Kodi 22 (Piers) to a modded Switch running Atmosphère/Horizon OS,
built with devkitPro in WSL, plus the effort to get it listed in the Homebrew App Store.

The hard parts are already solved: it boots, plays 1080p 10-bit HEVC with NVDEC, has
working Bluetooth audio, screenshots, correct clock, real memory reporting, and
installs official-repo skins and sounds. What remains is a small number of known gaps
(§6) and one listing that is waiting on a human reviewer.

---

## 2. How to work with this user

These are standing instructions, not preferences:

- **Measure first, infer second.** A claim needs a log line, a binary read, a DB query,
  or the user's eyes on the screen. "It should work" is not evidence.
- **No more guessing. It's costly.** If you cannot measure it, say so and name the one
  experiment that would settle it.
- **Confirm things on the user's end.** Their confirmation is the acceptance test for
  anything user-facing. RPC listings are not usage and have produced false bugs.
- **Check the notes constantly**, especially the *deliberate* changes (§8) — they look
  like bugs if you don't know why they exist.
- **Refer to the Homebrew community** for platform and error facts.
- **Read your own diff before pushing.** They have had to ask "did you check your code
  for errors?" and that question should never be necessary.
- Anything in `C:\Users\kidbu\Videos` must appear in Kodi and play as-is (no transcoding).
- No DRM circumvention — diagnosis only.
- Analog sticks are low priority. The store listing is the priority.

---

## 3. Environment

| Thing | Value |
|---|---|
| Console | `192.168.1.191` (Switch, Atmosphère) |
| PC | `192.168.1.234` |
| Kodi JSON-RPC | port **9090**, **raw TCP**: 4-byte big-endian length + JSON, reply is **bare JSON** with no prefix |
| Python probe | port **9998** (`script.pyprobe`), runs Python inside Kodi |
| Media server | port **8003**, serves `C:\Users\kidbu\Videos`, autostarts via Startup folder |
| Build env | WSL Ubuntu, user `kidbu`, devkitPro |
| Card when pulled | mounts as `D:\` |
| Kodi data on card | `D:\switch\kodi\.kodi\` |
| Logs | `D:\switch\kodi\temp\kodi.log` |

**Port 9090 is not HTTP.** An HTTP request to it returns a header-less
`HTTP/1.1 400 Bad Request` — that means "wrong protocol", not "web server broken".

---

## 4. Access and tooling

Helper scripts in `C:\dev\kodi-switch\` (all run through WSL):

| Script | Purpose |
|---|---|
| `kodi_rpc.py` | raw TCP JSON-RPC helper; `rpc(method, params, timeout=)` |
| `probe_run.py` | POST a Python file to the probe; **needs `start_probe2.py` first** |
| `start_probe2.py` | enables + executes `script.pyprobe`, waits for the listener |
| `check_alive.sh` | ping + 9090 + 9998 reachability |
| `rebuild.sh` | sync overlay → apply → build → package |
| `wsl-package-kodi.sh` | packages add-on folders (strips impossible imports, zero-byte `.a`) |
| `build_iptv_namespace.sh` | builds `pvr.iptvsimple` with `-Dkodi=iptv_kodi` + prefixed entry points |
| `get_latest_album.sh` | pulls newest screenshots from the console album |

Gotchas that waste a session if forgotten:

- The probe's listener binds reliably only in the **first ~30–60 s** after a Kodi start.
  Do file work early. If it stops binding, `SetAddonEnabled false→true` recreates the
  interpreter and usually restores it.
- `kodi.log` is **locked** while Kodi runs (`EIO`). Readable alternatives:
  `kodi.old.log` (previous session) — note `boot.log` does **not** exist on this build.
  The practical way to read the current session is to restart Kodi.
- FAT refuses a second open **and** refuses `rename` on a file Kodi holds. That is why
  DB repair runs from a preinit hook instead of at runtime.
- Socket pool is a small fixed budget (~9 sockets measured). A 7 MB skin install
  exhausts it and kills JSON-RPC until it finishes. Don't run a big install and an RPC
  test at the same time.

---

## 5. Build and deploy

```bash
# WSL
source /etc/profile.d/devkit-env.sh
source /opt/devkitpro/switchvars.sh      # exports CPPFLAGS/LDFLAGS/LIBS=-lnx
cd /mnt/c/dev/kodi-switch && ./rebuild.sh
```

- `CPPFLAGS` **must** include `-isystem $DEVKITPRO/libnx/include` or every autotools
  network check fails silently.
- Autotools looks for `aarch64-none-elf-pkg-config` and falls back to `false`; pass
  `--pkg-config=/usr/bin/pkg-config`.
- Deploy the `.nro` via netloader (`wsl-nxlink-run.sh`) — **needs a physical `Y` press**,
  one shot. If it errors, power-cycle the console.
- Homebrew gets full memory only when hbmenu is started from a title with `R` held, not
  in Album applet mode.
- Add-on folders deploy as a zip on the media server + probe extract, or on the card.

---

## 6. State as of 2026-11-16

### Deployed
A **local build**, newer than `v22.0rc3`. It includes the DB-repair preinit hook and the
static-linked `pvr.iptvsimple`. The PVR add-on folder on the card is **parked** as
`pvr.iptvsimple.off` so Kodi boots normally.

Confirm the build actually running by reading the log's
`Starting Kodi (22.0-RC1 (21.90.901) Git:...)` line — do not assume.

### Working, verified on hardware
| Area | Evidence |
|---|---|
| Playback | 1080p 10-bit HEVC via NVDEC; H.264 software path; seek/resume/replay |
| Thread teardown | 0 ENOSYS escapes in 18.7 min (was 1 by playsec=65) |
| Clock | correct local time (was `mktime` where `timegm` belonged) |
| System info | `195MB / 3084MB - 6%`, "Atmosphère on Horizon" |
| Screenshots | 1280x720 PNGs written |
| Bluetooth audio | user: "Perfect" |
| Screen dimming | fixed during playback (auto-sleep disabled while media plays) |
| JSON-RPC | accept loop no longer spins ~8,000/s; no client leak |
| Official repo | `skin.confluence` installed and running; skins + UI sounds install |
| Add-ons | `plugin.video.watchnixtoons2` menus + playback **confirmed by user** |

### Parked / blocked
- `pvr.iptvsimple` — loads, then the PVR manager stalls (§7.1). Parked.
- `plugin.video.thecrew` — deliberately left disabled (§8).
- Battery percentage reads 0% (`psmInitialize` succeeds). Needs an independent oracle.
- Card work: `h2testw`, homebrew inventory, temp-folder cleanup.

### Store listing
PR **#1078** on `fortheusers/switch-hbas-repo`: 3 commits, 7 files
(`pkgbuild.json` + icon + 5 screenshots), no binaries, validates with 2,699 manifest
entries. Points at release `v22.0rc3`. **0 comments, 0 reviews.**

Measured merge pace in that repo: **1–3 days** (new packages ~2 days). Ours has been
open longer than that with no reviewer activity. Nothing to fix from our side; if they
ask, likely topics are package size or the runtime layout.

---

## 7. Open work, in priority order

### 7.1 PVR stall — the real blocker (needs one instrumented build)

**Symptom:** PVR manager sits at "PVR manager is starting up" forever; 0 channels;
`PVR.GetChannels` → `-32100 Failed to execute method`.

**Pinned by source + log:** `CIptvSimple::Start()` calls `m_playlistLoader.LoadPlayList()`
**synchronously** before it logs `"Starting separate client update thread..."`. That line
never appears in `kodi.log`, so the stall is inside `LoadPlayList()`.

**Ruled out, each with evidence:**

| Suspect | Evidence |
|---|---|
| settings | `addon_data/pvr.iptvsimple/settings.xml` still holds `m3uPathType=1`, `m3uUrl=http://192.168.1.234:8003/test.m3u` |
| media server | `GET /test.m3u` → 200, 295 bytes |
| our curl/VFS path | probe `xbmcvfs.File(<same url>).read()` → 295 bytes in 0.0 s |
| databases | `PRAGMA integrity_check` = ok on TV48, Addons33, MyVideos149, Textures14, Epg22 |
| retry logic | the fetch is instant, so retries aren't stalling it |
| add-on load | `PVR_ADDON_Create returned 0` — the shim works |

**Leading hypothesis, unproven:** PVR starts ~14 s into boot, before WiFi association
completes, so the connect blocks. Kodi logs nothing about network state at info level.

**Next step:** build an instrumented image that traces nifm/interface state at preinit and
around the first `CCurlFile::Open` of an `http://` URL. If timing is confirmed, either
delay PVR start or give curl a connect timeout — both small.

Also observed in the same log: at 08:31 the console could not reach `mirrors.kodi.tv`
(`CCurlFile ... Could not connect to server`) while the LAN server answered fine.
Internet reachability deserves its own check.

**Do not** toggle the add-on with `SetAddonEnabled` while the PVR manager is starting —
it aborts the start and fabricates a "could not validate M3U" failure.

### 7.2 Store PR
Watch #1078 for reviewer comments. Nothing actionable until someone responds.

### 7.3 Battery
Still 0%. Suggested independent oracle: PyNX on the console, to prove whether the
hardware/psm reading or our code is wrong.

### 7.4 Card maintenance (when the card is next out)
- clear test junk from `D:\switch\kodi\temp\` (`wnt2*.zip`, `stress_*.bin`, several
  20 MB repo indexes, `sl.b64`, `req.zip`, `t.txt`)
- run `h2testw` on the card
- inventory installed homebrew `.nro` under `/switch/`

---

## 8. Deliberate changes — do NOT "fix" these

- **Socket tuning:** small per-socket buffers, few bsd sessions, `CURLOPT_FORBID_REUSE`,
  and transient retries in our `CurlFile` overlay. These are load-bearing; the pool is a
  fixed budget and these settings are what make it survivable.
- **`SO_LINGER {1,0}` on curl sockets was tried and reverted** — it wedged Kodi in the
  busy dialog (window 10138). Do not re-add it.
- **`plugin.video.thecrew` left disabled.** It declares `xbmc.service`, so it runs on
  every start and is a known socket-pool consumer. User's call: leave it off unless it
  breaks something else. Nothing depends on it (`script.module.resolveurl` is enabled).
- **Guard gated on `appPlayer->GetTime() > 5000`**, not a timer.
- **No `svcSetHeapSize`** at startup — the default heap is already ~3 GB and calling it
  again makes Kodi exit with no crash report.
- **`skin.confluence`** is the active skin.

---

## 9. Traps that have already cost sessions

| Trap | Reality |
|---|---|
| `pthread_detach()` | stub returning `ENOSYS`; `std::thread::detach()` always throws. Park the thread + join on a reaper. |
| `<dlfcn.h>` | devkitPro ships inline `dlopen`/`dlsym` stubs; any TU including it binds to them. Use `switch_*` names. |
| Binary add-ons | cannot `dlopen`. Static-link with **prefixed entry points and a per-add-on SDK namespace** (`-Dkodi=iptv_kodi`). `objcopy --redefine-sym` and `-Wl,--allow-multiple-definition` both failed. Create a zero-byte `<name>.a` so Kodi's existence check passes. |
| Add-on string tables | loaded when the add-on loads, i.e. at startup. Installing while running ⇒ empty labels until restart. |
| `Settings.SetSettingValue` | rejects add-on settings (`Invalid params`). Write `addon_data/<addonid>/settings.xml`. |
| Screenshots | Kodi 22 routes through `CCaptureService`; only DRM-PRIME implements it, so `TakeScreenshot` silently writes nothing. |
| `Addons.GetAddons` bulk | its `enabled` field proved unreliable, and it omits `kodi.binary.*`/`xbmc.*`. **`Addons.GetAddonDetails` is the oracle.** Don't put `type` in its `properties` list — that makes every call return `-32602` and looks like mass breakage. |
| SQLite on FAT | `mode=ro` gives `disk I/O error` (it tries to lock). Use `mode=ro&immutable=1`. |
| `Addons33.db` rebuild | wipes **every** add-on's enabled flag (measured: 106 of 117). Restore from `Database.build-broken/Addons33.db`, table `installed`, column `enabled`. |
| `mallinfo().arena` | high-water mark, not the limit. Real ceiling is `fake_heap_end - fake_heap_start`. |
| Periodic gaps | a perfect 1.000 s period is a timer, not congestion. |
| RPC harness | not a usage simulation. It produced ≥3 false bugs and 1 real regression. |

---

## 10. First 15 minutes in a new session

1. Read `CHECKLIST.md`, then §2 and §8 above.
2. `bash check_alive.sh` — confirm the console answers on 9090.
3. Ask the user what they want to work on and **what they see**, not what you assume.
4. If resuming PVR: rename `pvr.iptvsimple.off` → `pvr.iptvsimple` on the card (needs the
   card, or the probe if the folder is on the card and Kodi is stopped), then do the
   instrumented build in §7.1.
5. If resuming the store listing: `gh pr view 1078 --repo fortheusers/switch-hbas-repo`.
6. Anything you learn that a future session could get wrong goes into
   `docs/PORT-STATUS.md`; state changes go into this file or `HANDOVER.md`.

---

## 11. File map

```
C:\dev\kodi-switch-source\
  CHECKLIST.md              mandatory process checklist
  HANDOVER.md               earlier handover (2026-11-14), still accurate on the fixed list
  MIGRATION.md              this file
  docs\PORT-STATUS.md       full development log, newest at bottom
  docs\ARCHITECTURE.md      port layout
  docs\BUILDING.md          build instructions
  docs\ROADMAP.md           remaining work
  overlay\                  files copied over the Kodi tree at build time
    xbmc\platform\switch\PlatformSwitch.cpp   static-addon shim, DB repair hook, socket gauge
    xbmc\threads\Thread.cpp                   reaper + build stamp
    xbmc\application\Application.cpp          playback guard, media/auto-sleep, mem trace
    xbmc\platform\posix\XTimeUtils.cpp        UTC conversion + timezone bias
    cmake\scripts\switch\ArchSetup.cmake      DEPLIBS (no xbmc/ prefix in those paths)
```

WSL-only paths used during the PVR work:
`/home/kidbu/kodi-switch/pvr.iptvsimple` (add-on source),
`/home/kidbu/kodi-switch/kodi` (Kodi source tree).
