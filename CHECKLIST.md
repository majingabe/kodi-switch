# Mandatory checklist

Every item here exists because skipping it already cost us a session. Tick them in
order; if an item cannot be ticked, say so out loud instead of proceeding.

Rule zero: **measure first, infer second.** A claim needs a log line, a binary read,
a DB query, or the user's eyes on the screen. "It should work" is not evidence.

---

## A. Before touching anything

- [ ] **Read the notes.** `HANDOVER.md`, then the last ~200 lines of
      `docs/PORT-STATUS.md`. Deliberate changes live there and will look like bugs
      otherwise (dimming, socket buffers, `FORBID_REUSE`, retry logic, the shim).
- [ ] **Write down the current state** so you can tell change from noise:
      deployed build stamp, `git log --oneline -1`, what the user says is broken.
- [ ] **Ask what the user sees**, not what you assume they see. "Broke my add-on"
      covers missing / present-but-off / starts-and-errors — three different causes.
- [ ] **Confirm the console is reachable** (`check_alive.sh`: ping, 9090, 9998)
      before planning any console work.

## B. Before any database or add-on surgery

- [ ] **Copy the DB first.** Rename to `*.corrupt` / `*.bak` so the old enabled set is
      recoverable — it is the only record of what the user actually had on.
- [ ] **Know that a rebuild wipes enabled flags.** After any `Addons33.db` rebuild,
      **every** add-on comes back `enabled: false` (measured: 106 of 117).
      Re-enable the user's real set before calling the job done.
- [ ] **Recover the set from the old DB, don't guess it.** The `installed.enabled`
      column of `Database.build-broken/Addons33.db` is authoritative. Read it with
      `mode=ro&immutable=1` — plain `mode=ro` returns `disk I/O error` on FAT
      because SQLite tries to take a lock.
- [ ] **Diff old vs current and re-enable the difference**, then verify each one
      reports `enabled=True` through `Addons.GetAddonDetails`.
- [ ] **Exercise it for real.** A root menu can be built locally; the add-on is only
      proven when a listing that hits the network returns items
      (nixtoons: root 11 items, `?action=actionCatalogMenu&path=latest` 16 items).

## C. Before trusting a test result

- [ ] **Is this a usage simulation or the real thing?** The RPC harness produced at
      least three false bugs and one real regression. Never toggle add-ons while the
      PVR manager is starting — it aborts the start and fabricates
      "could not validate M3U".
- [ ] **Did the code path actually run?** Absence of a log line is evidence
      (`Start()` never logged its thread line ⇒ stuck inside `LoadPlayList()`).
- [ ] **Are you reading the right log level?** Add-on `LEVEL_DEBUG` lines vanish at
      the default level; a "silent" add-on may just be unlogged.
- [ ] **Separate the link from the process.** netloader speed ≠ app speed; raw socket
      ≠ library; a 31 MB/s PC mirror proved the console was the ceiling.
- [ ] **A perfect 1.000 s period is a timer**, not congestion. Periodic gaps mean a
      poll loop, a refill tick, or a throttled progress dialog.
- [ ] **`mallinfo().arena` is a high-water mark, not a limit.** Real ceiling is
      `fake_heap_end - fake_heap_start`.

## D. Before deploying

- [ ] **Build stamp matches the commit** you think you deployed. Check the log's
      `Starting Kodi ... Git:` line.
- [ ] **Nothing is holding the files.** FAT refuses a second open and refuses
      `rename` while Kodi holds a file — that is why `kodi.log` reads `EIO` and why
      DB repair runs from a preinit hook. If you need file access, exit Kodi and pull
      the card.
- [ ] **Socket budget.** The pool is a small fixed budget (~9 sockets measured). A
      7 MB skin install exhausts it and kills JSON-RPC until it finishes. Do file and
      network work early in a session; the probe's listener binds reliably only in
      the first ~30–60 s after a start.
- [ ] **Do the probe step in order**: `start_probe2.py` first, *then*
      `probe_run.py`. Running the client alone gives `Connection refused` and looks
      like a device failure.
- [ ] **netloader needs a physical `Y` press**, one shot. If it errors, power-cycle.

## E. Before saying "fixed"

- [ ] **Verified on hardware, by the user**, for anything user-facing. Their
      confirmation is the acceptance test, not your inference.
- [ ] **The specific symptom is gone**, not a nearby one.
- [ ] **Nothing else regressed**: boot to UI, playback, exit, JSON-RPC, add-on list.
- [ ] **Leave the console usable.** Park half-finished work (`*.off`) rather than
      shipping a boot that stalls — a stuck PVR spinner is worse than a missing
      feature.

## F. Before committing / pushing

- [ ] **Read your own diff.** Re-check the shim, link flags and packaging line by
      line when they are involved — the user has had to ask "did you check your code
      for errors?", and that question should never be necessary.
- [ ] **Commit message says what was measured**, not what was hoped.
- [ ] **Notes updated**: new facts into `docs/PORT-STATUS.md`, state into
      `HANDOVER.md`. Anything a fresh session could get wrong belongs in the notes.

---

## Permanent traps (do not rediscover)

| Trap | Reality |
|---|---|
| `pthread_detach()` | stub returning `ENOSYS`; `std::thread::detach()` always throws. Park + join on a reaper. |
| `<dlfcn.h>` | devkitPro ships inline `dlopen`/`dlsym` stubs; any TU including it binds to them. Use `switch_*` names. |
| `svcSetHeapSize` | do **not** call on an already-configured heap — app exits right after the window appears, no crash report. |
| Binary add-ons | cannot `dlopen`; static-link with prefixed entry points **and** a per-add-on SDK namespace (`-Dkodi=iptv_kodi`). `objcopy --redefine-sym` and `--allow-multiple-definition` both failed. |
| Add-on string tables | loaded when the add-on loads, i.e. at startup. Install while running ⇒ empty labels until restart. |
| `Settings.SetSettingValue` | rejects add-on settings (`Invalid params`). Write `addon_data/<addonid>/settings.xml`. |
| Screenshots | Kodi 22 routes through `CCaptureService`; only DRM-PRIME implements it, so `TakeScreenshot` silently writes nothing. |
| Port 9090 | Kodi's **raw TCP** JSON-RPC: 4-byte BE length prefix, bare JSON reply. An HTTP request returns a header-less `400` — that is "wrong protocol", not "web server broken". |
| `SO_LINGER {1,0}` on curl sockets | tried and reverted; wedged Kodi in the busy dialog (window 10138). |
| Add-on services | a plugin can declare `xbmc.service` and run on every start; that is what exhausts the socket pool. `plugin.video.thecrew` is one. |

---

## Standing asks from the user

- Always refer to the Homebrew community for platform and error facts.
- Confirm things on the user's end before declaring them fixed.
- Anything in `C:\Users\kidbu\Videos` must appear in Kodi and play as-is (no transcoding).
- No DRM circumvention — diagnosis only.
- Analog sticks are low priority; the store listing is the priority.
