# Roadmap

Ordered roughly by value per unit of pain. Nothing here is promised; the port is a
hobby project and this list is a map, not a contract.

## Experiment queue (cheap, answers open questions)

**Does launching from a forwarder make the exit fast?** — **ANSWERED: yes.**
Measured on hardware: launched from an NSP forwarder (a normal application title), exit is
immediate and clean; from hbmenu it takes ~30 s. So the delay was hbloader tearing down the
applet, not Kodi's shutdown, and there is nothing to fix in this code. Ship/launch from a
forwarder.

**Do stale "broken title" records clear on their own?**
After repairing tickets, some titles still refuse to start for a while. Test whether a
reinstall clears the record, whether a reboot alone does, and whether maintenance-mode
cache clear is ever required.

## Near term

**Faster shutdown (~30 s → a few seconds).**
`CApplication::Stop()` returns early and `Run()` skips `Cleanup()`, so Kodi's threads
are torn down by process exit rather than in order, and the libnx services the port
starts (socket, ssl, nifm, hid) are never released. Calling the cleanup explicitly after
`XBMC_Run()` returns — instead of relying on `atexit`, which does not appear to run — is
the cheap experiment.

**Close the `ENOSYS` teardown leak.** — **DONE (2026-11-13).**
Root cause was not the future wait: `pthread_detach()` is a stub returning `ENOSYS`,
and auto-delete `CJobWorker`s destroy themselves from their own thread, where joining is
impossible, so `~CThread` reached `detach()` on every finished job. `Join()` now polls a
completion flag, and the destructor parks self-destroying threads for the next `Create()`
to `join()` + `delete` (which reclaims the handle). Verified with zero escapes across a
long playback.

**Hardware H.264 decode.**
NVDEC currently only takes HEVC, deliberately, so a hardware bug cannot regress the
proven software path. With the HEVC path stable, H.264 can be added behind the same
guard rails, which would cut CPU and power use for the most common content.

**Frame buffer pooling.**
The NVDEC path allocates a P010 frame plus an NV12 frame per decoded frame. It works and
memory stays flat, but a small pool keyed by size and format would remove tens of MB/s
of allocator churn and make very long sessions safer.

## Medium term

**Live TV / PVR.**
Kodi needs a binary PVR client, and this platform cannot `dlopen`, so `pvr.iptvsimple`
must be built as a static library and linked in, exactly like `inputstream.adaptive`.
Its dependencies are comparatively kind (pugixml/tinyxml2, zlib, curl — no ffmpeg).
That unlocks M3U + XMLTV, EPG, channel groups and recording.

**A small management HTTP endpoint in the binary.**
The Python probe used during development (`script.pyprobe`) works but competes for the
same bsd socket pool as Kodi itself. A few hundred lines of libnx HTTP inside the
process would make file operations, log reads and restart-free deploys reliable.

**Add-on socket hygiene.**
An add-on that keeps a `requests` session alive across hosts will exhaust the pool
(`ENOBUFS`, errno 105). A documented `Connection: close` patch fixed one popular add-on
completely. Longer term: make Kodi's Python layer reap idle sockets.

## Long term / research

**In-process torrent streaming (Elementum-style).**
Free sources without a debrid account. The catch is architectural: Elementum runs a
separate native daemon that the Python add-on talks to, and Horizon has **no fork/exec**,
so the daemon cannot exist here. The only viable shape is libtorrent compiled for
`aarch64-none-elf` and linked into `kodi.bin`, with a C++↔Python bridge. That means
porting libtorrent and boost, and managing its threads inside Kodi's process. A
multi-session project, not an add-on install.

**Generic binary add-on support.**
The `switch_dlopen` shim is per-add-on today. A generated dispatch layer (one namespace
per add-on, selected at load time) would make porting further binary add-ons mechanical
rather than bespoke.

**Colour management.**
The renderer is 8-bit NV12 only. 10-bit output, HDR signalling and correct BT.2020
handling would need a different GLES path and, realistically, more GPU bandwidth than
the stock clocks allow.

**Analog sticks.** — **DONE (2026-11-22).** `CWinEventsSwitch::PumpStick()` maps the left
stick onto the four directional keys: ~1/3 deadzone, dominant axis wins, one press on
entry then 400 ms delay and 120 ms repeat while held. The right stick is still unused.
The proper `IJoystickDriver` route (per-axis mapping, analog scrolling, controller
profiles) remains open if menu navigation ever needs more than this.

**Better input.**
Motion controls, rumble, and multiple controllers with per-player mapping are all
reachable through libnx; none are wired up yet.
