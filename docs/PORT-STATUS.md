> Note: addresses appear as `<switch-ip>` / `<pc-ip>` and some paths as the
> original developer's; this is a raw log from one development machine.

# Kodi on Nintendo Switch — port status

## Working on hardware
- Kodi 22.0rc1 boots to the Estuary GUI; stable in normal use
- Controller: D-pad, A/B/X/Y, L/R, ZL/ZR, +/-, **analog stick navigation** (new)
- Audio: `CAESinkSwitch` (libnx audout) -> speaker / headphones / Bluetooth
- Video: FFmpeg H.264 software decode + GLES renderer, OSD, seeking, clean exit
- Databases (SQLite), skin, addons, settings
- Network: JSON-RPC **9090**, EventServer **9777**; remote control verified
- Unattended bring-up: TCP trace server on **9998** (readable with `nc <ip> 9998`,
  accepts `quit` to exit Kodi), pushed over Wi-Fi with nxlink

## Known gaps
- HTTP web UI (8080): `CNetworkServices::Start()` only starts it when
  `services.webserver` is true *at network-up time*. After a failed start Kodi
  resets the setting to false, so it never retries. libmicrohttpd is now fixed
  (`kodi_switch_socket_pair[_nblk]`), so **toggling it on in the UI should work**.
  Trace lines: `services: webserver setting is ON/OFF`, then `webserver: ...`.
- Python: interpreter integrated and Kodi builds/links with it, but not yet run on
  hardware. Missing extension modules: `_ssl`/`_hashlib` (our OpenSSL is built
  with `no-sock` + no threads), `_sqlite3`, `_lzma`.
- inputstream.adaptive not built
- 1080p+ (NVDEC), JPEG thumbnails, SMB/NFS shares, BT latency

## Python (P2) — integration state
- CPython **3.14.6** cross-built for `aarch64-none-elf`
  - src: `~/kodi-switch/pybuild/Python-3.14.6`
  - build: `~/kodi-switch/pybuild/build-switch/libpython3.14.a` (63 MB)
  - prefix for Kodi: `~/kodi-switch/python-prefix` (headers + lib + .pc)
  - static builtin modules: `_socket`, `select`, `binascii`, `zlib`, `_bz2`
    (via `Modules/Setup.local`), plus all core builtins
- Kodi build: `ENABLE_PYTHON=ON`, `ENABLE_INTERNAL_SWIG=OFF`,
  `SWIG_EXECUTABLE=/usr/local/bin/swig` (SWIG 4.5.0 built for the host)
- Kodi links `libpython3.14.a` twice (archive ordering) via DEPLIBS
- Stdlib shipped in the package at `kodi/python/lib/python3.14` (12 MB, pruned)
- `switch_preinit()` sets `PYTHONHOME=/switch/kodi/python` only if the dir exists
- Test program: `~/kodi-switch/pytest/pytest.nro` (5 MB) — prints version, runs
  `print()` and `sum(range(1000))`

### CPython patches applied
- `configure`: accept `aarch64-none-elf` (MACHDEP=switch, ac_sys_system=Generic)
- `Include/pyport.h`: `PY_SSIZE_T_MAX` fallback (newlib lacks `SSIZE_MAX`)
- `Modules/timemodule.c`: `_timezone/_daylight/_tzname`; `long` clockid
- `switch-config.site`: no fork/exec/popen/mmap/dlopen/statvfs/fdatasync/fchdir/
  chroot/pause, no sys/resource.h, `--without-mimalloc`, `--disable-ipv6`

### To finish Python
1. Run `pytest.nro` (applet mode) -> confirm interpreter works on hardware
2. Run Kodi -> check `service.xbmc.versioncheck` starts (it is a Python service)
3. For https add-ons: rebuild OpenSSL for Switch **with sockets and threads**
   (`./Configure linux-aarch64`-style with `-D__SWITCH__`, no `no-sock`), then add
   `_ssl _ssl.c -lssl -lcrypto` and `_hashlib _hashlib.c -lcrypto` to
   `Modules/Setup.local` and rebuild libpython
4. `_sqlite3` lives in `Modules/_sqlite/` in 3.14 - needs its own Setup entry
5. `_lzma` needs an lzma portlib (not present)

## Deploy / test loop
- Build:  `wsl -d Ubuntu -u kidbu -- bash /mnt/c/dev/kodi-switch/wsl-build-kodi.sh`
- Package:`wsl -d Ubuntu -u kidbu -- bash /mnt/c/dev/kodi-switch/wsl-package-kodi.sh`
- Stage:  `wsl -d Ubuntu -u kidbu -- bash /mnt/c/dev/kodi-switch/wsl-stage-sd.sh`
- Push:   `wsl -d Ubuntu -u kidbu -- bash /mnt/c/dev/kodi-switch/wsl-push-now.sh`
  (hbmenu full-memory via R on eShop/NSO, then press **Y** for netloader)
- Trace:  `nc <switch-ip> 9998`
- Quit:   `printf 'quit\n' | nc <switch-ip> 9998`
- SD logs: `/switch/kodi/boot.log`, `/switch/kodi/temp/kodi.log`

## Key overlay changes
`overlay/` contains: no `-fPIC` (TLS), SQLite shared-descriptor VFS + DB seeding,
`CLOCK_MONOTONIC` clock, GLES video renderer registration, libnx init (sockets,
nifm, hid + padConfigureInput), nifm network interface, `pipe`/socketpair shims,
audout sink, trace server + remote quit, webserver trace points, analog sticks,
`ENABLE_PYTHON` + SWIG + libpython wiring, `ENABLE_MICROHTTPD`.

## Regression to bisect (2026-11-10)
Last known-good: build ~19:20 (GUI/audio/video working, ENABLE_PYTHON=OFF, ENABLE_MICROHTTPD=OFF,
no trace thread, no analog sticks).

Symptoms after later changes:
- Python-enabled build: exits cleanly right after `InitStageOne done` (dies inside
  CApplication::Create / service init, where XBPython + Py_Initialize run)
- Python-disabled but MHD-on + trace-thread + sticks: splash -> black -> freeze ->
  "software unable to start"
- Exiting full-memory Kodi sometimes takes the whole console down (teardown)

Bisect plan (re-add one at a time):
1. baseline = Python off, MHD off, trace server not started, sticks off  (built)
2. + analog sticks
3. + trace server (started from MessagePump)
4. + ENABLE_MICROHTTPD (webserver setting off)
5. + webserver setting true (expect MHD start path)
6. + Python (validate pytest.nro first)

Note: `services.webserver` in guisettings was set back to false.

## 2026-11-10 late: suspicion + revert staged
Every build since the trace rewrite (~20:50) has failed to boot:
- trace server in preinit -> died just after `InitStageOne done`
- trace server lazy + Python -> same
- Python off + MHD off + no trace server + no sticks (still carrying the
  rewritten switch_trace with std::mutex + std::deque ring buffer) -> also died

`switch_trace` is called from a static constructor at the very top of startup and
now used a function-local `std::mutex` and `std::deque`, plus a background thread.
That is the common change across all failures -> prime suspect.

Reverted (built, staged, NOT yet tested on hardware):
- `switch_trace` = plain open/write/printf again (no mutex, no deque, no thread)
- removed TraceServerThread, remote quit, ring buffer, and the pump hooks
- removed analog-stick navigation (for now)
- `ENABLE_PYTHON=OFF`, `ENABLE_MICROHTTPD=OFF`

Next when console is available:
1. push `package/kodi.nro` (hbmenu full-memory + press Y, then wsl-push-now.sh)
2. probe 9090 -> if up, baseline is restored; then re-add ONE feature at a time:
   analog sticks -> trace server -> microhttpd -> webserver setting -> Python
3. keep `boot.log` (do not delete) so the failing step is visible; also keep any
   Atmosph�re crash report from `atmosphere/crash_reports`

## 2026-11-10 resolution: applet mode was the red herring
The "regressions" were applet-mode launches (hbmenu started from Album, ~400 MB heap).
Kodi needs >1 GB, so it died mid-startup ("splash -> black -> software unable to start"),
and earlier builds died even sooner.

Verified: same code, pushed via netloader from a **full-memory** hbmenu (R on eShop/NSO)
-> booted, JSON-RPC 9090 came up.

Workflow rule from now on:
- ALWAYS launch hbmenu with R on eShop/NSO before pressing Y for netloader.
- Applet mode is only for tiny probes (iotest / mhdtest).
- Do not delete boot.log/kodi.log before a test: keep evidence.

Current: Python-enabled build pushed for a full-memory test (expect 9090 + python service).

## 2026-11-10 13:35 - Python build failure localised
boot.log (full memory, Python build) ends at:
    audio sink registered
    InitStageOne done
The next statement in CServiceManager::InitStageOne() is
    m_XBPython = std::make_unique<XBPython>();
so Kodi dies inside the XBPython constructor - i.e. Py_Initialize() or the
binding-module imports, before Kodi's log file is even opened (no new session in
kodi.log confirms it).

Prepared for the next Python build:
- overlay/xbmc/interfaces/python/XBPython.cpp now traces to boot.log:
  "XBPython ctor enter" -> "python builtin modules registered" ->
  "Py_Initialize enter" -> "Py_Initialize done" ->
  "python binding modules imported"
  (guarded by TARGET_SWITCH; the SWIG modules themselves unchanged)
- pytest.nro rebuilt (logs to sdmc:/switch/kodi/pytest.log, tests stdlib import,
  _socket/select/binascii/zlib/_bz2/_ssl/_hashlib/_sqlite3/_lzma/_ctypes,
  json/sqlite3/ssl/hashlib/urllib.request/zipfile, and a file write)

Card state: working baseline restored as /switch/kodi/kodi.nro,
Python build kept as /switch/kodi/kodi-python.nro, /switch/pytest.nro added.

## 2026-11-10 13:45 - ROOT CAUSE of the Python crash
pytest.nro on hardware captured CPython's own fatal message (stderr redirected to
sdmc:/switch/kodi/pytest.err):

    Fatal Python error: _Py_HashRandomization_Init: failed to get random numbers
    to initialize Python
    Python runtime state: preinitialized

Horizon OS has no /dev/urandom and the cross-build found no getrandom()
(pyconfig.h: HAVE_GETRANDOM / HAVE_GETENTROPY / HAVE_GETRANDOM_SYSCALL all undef),
so pyurandom() falls through to dev_urandom() and aborts. That is why the
Python-enabled Kodi died inside the XBPython constructor (Py_Initialize), and why
pytest took the console down.

Fix (applied): patches/python-switch-urandom.py adds a __SWITCH__ branch to
Python/bootstrap_hash.c that fills the buffer with libnx randomGet() (kernel
CSPRNG). It also covers os.urandom() at runtime, which would otherwise fail the
same way. wsl-rebuild-python-modules.sh now applies the patch automatically.

Build/verify order:
1. rebuild libpython3.14.a (incremental: wsl-rebuild-libpython.sh)
2. rebuild pytest.nro, run it on hardware -> expect all imports to report OK
3. rebuild Kodi with ENABLE_PYTHON=ON, push, expect 9090 + python service

## 2026-11-10 13:50 - CPython validated on hardware
pytest.nro (patched libpython) now completes:
    calling Py_Initialize
    initialized
    script done
    finalized
    === done ===
No crash, Py_Finalize clean. So the interpreter is usable; the earlier aborts
were purely the missing entropy source.

Known gap visible in pytest.err: hashlib has no algorithms at all
("ERROR:root:code for hash md5 was not found" ... md5/sha1/sha224/256/384/512),
because neither _hashlib nor the builtin _md5/_sha* modules are compiled in.
Consequences: hashlib.* unusable, and ssl/urllib HTTPS unusable until OpenSSL is
rebuilt with socket+thread support and _ssl/_hashlib are added to Setup.local.
Also still missing: _sqlite3 (libsqlite3.a is available in portlibs) and _lzma
(no liblzma in devkitPro portlibs yet).

pytest + button fix: padConfigureInput() was missing, so the app got no input.

Next: push Python-enabled Kodi (built against patched libpython) and check
boot.log for "XBPython ctor enter" -> "Py_Initialize done" ->
"python binding modules imported".

## 2026-11-10 14:05 - build-system bug: stale overlay objects
wsl-apply-overlay.sh used `cp -a`, which preserves the Windows mtimes. Ninja
compares source vs object mtime, so any overlay file older than its object was
silently NOT rebuilt. Proof: the ELF contained no "services: webserver setting"
string even though the source in the tree has that trace - the linked
NetworkServices.cpp.o predated the edit.

Fixed: wsl-apply-overlay.sh now touches every file it copies, so all patched
translation units are rebuilt on the next build.

Also added (diagnostics):
- ServiceManager.cpp in the overlay: traces "SM InitStageOne enter",
  "SM InitStageOne: creating XBPython", "SM InitStageOne: XBPython created"
- removed the per-button "pad buttons 0x..." trace (it flooded boot.log)
- XBPython.cpp traces (ctor enter / Py_Initialize enter+done / binding modules)

Open question this build answers: the previous run's boot.log had no XBPython
trace at all and no "initializing python engine." in kodi.log, yet
service.xbmc.addonmanager tried to start service.xbmc.versioncheck and failed -
i.e. Python may not have been initialised at all in that binary. The SM traces
will show whether InitStageOne reaches the XBPython block.

Shutdown: log ends at "Stopping all" then nothing; console does not crash but
Kodi never finishes exiting (hang in teardown, likely Py_Finalize).

## 2026-11-10 14:30 - PYTHON BUILD BOOTS (milestone)

Kodi 22 with Python now reaches the GUI and stays running. Confirmed two ways:
boot.log reached "Py_Initialize enter" on the previous build (then died), and
the 14:15 relink (patched libpython installed into python-prefix/lib first)
runs and answers JSON-RPC.

### Why every earlier Python build died
Kodi links <project-dir>/python-prefix/lib/libpython3.14.a. That copy
was from 04:31, i.e. built BEFORE the entropy patch. The patched archive sat in
pybuild/build-switch/ and was never installed. Py_Initialize aborted in
_Py_HashRandomization_Init exactly where boot.log stopped.
Fix: both rebuild scripts now always copy the fresh archive into the prefix.

Note: grepping for "switch_urandom" in the archive gives 0 even when patched,
because the helper is `static` and inlined. Check for an undefined `randomGet`
in bootstrap_hash.o (or a `bl randomGet` in the final ELF) instead.

### JSON-RPC is fine - my test was wrong
Port 9090 is RAW JSON-RPC over TCP, not HTTP. Any request that parses as an
HTTP header block hits CWebSocketManager::Handle(), which answers 400 Bad
Request when Sec-WebSocket-Version is missing, and never feeds it to JSON-RPC.
curl therefore always "hung". Raw socket works:
  {"jsonrpc":"2.0","id":1,"method":"JSONRPC.Ping"} -> {"result":"pong"}
  Application.GetProperties -> Kodi 22.0 RC1 20260927-28ea2ea-dirty
EventServer 9777 and web UI 8080 are closed because both are off by default
(and the web interface is not compiled in this build).

Addon DB is healthy: service.xbmc.versioncheck, metadata.themoviedb.org.python
and metadata.tvshows.themoviedb.org.python are installed, enabled, not broken.

### Real cause of the thin stdlib (37 modules)
configure generates Modules/Setup.stdlib starting with `*shared*`. Horizon has
no dlopen, so every module in that section was silently never built. Only the
five hand-listed modules in Modules/Setup.local made it into libpython.
Fix: patches/python-switch-setup-stdlib.py rewrites the generated file to
`*static*` and drops what cannot build here (ctypes, multiprocessing, fcntl,
syslog, _posixsubprocess, _remote_debugging, _interpreters*, _lzma, pyexpat,
_elementtree). _sqlite3 is added explicitly in Setup.local (-lsqlite3; lib and
header are already in portlibs).

### OpenSSL blocks _ssl/_hashlib
portlibs has OpenSSL 3.5.5 but configured `no-threads no-sock no-dgram no-quic
no-dtls no-zlib`. libssl.a has no SSL_set_fd and no BIO_new_socket, so
CPython's _ssl cannot even link. Needs a rebuild with `threads sock` (keep
no-shared/no-async). Until then hashlib will work via the built-in _md5/_sha1/
_sha2/_sha3/_blake2 backends once the static build lands, but `ssl`/`urllib`
https will not.

Missing third-party libs for optional modules: libexpat, liblzma, libffi.

## 2026-11-10 14:33 - stdlib rebuilt: 67 modules

Root cause of the thin stdlib was Setup.stdlib being generated with `*shared*`
(no dlopen on Horizon -> nothing built). patches/python-switch-setup-stdlib.py
rewrites it to `*static*` and drops what cannot build here. Result: 67 built-in
modules, including math, cmath, _struct, _random, _json, _pickle, _csv, array,
_decimal, unicodedata, _sqlite3, the CJK codecs, and hashlib backends
(_md5/_sha1/_sha2/_sha3/_blake2).

Dropped: _ctypes, _multiprocessing, fcntl, syslog, _posixsubprocess,
_remote_debugging, _interpreters*, _lzma (no liblzma), pyexpat/_elementtree
(no libexpat), _uuid (shim lacks uuid_generate_time_safe), _hmac (would need
HACL HMAC objects wired into the static archive; hmac falls back to pure Python).

Two archive traps, both now automated in wsl-fix-python-hacl.sh:
1. Modules/_hacl/*.o and Modules/_decimal/libmpdec/*.o are compiled but never
   archived (their LDFLAGS only apply to shared builds) -> _blake2 and _decimal
   fail at link. They are spliced in.
2. `ar r` matches members by BASENAME, so Modules/_decimal/libmpdec/context.o
   replaced Python/context.o and broke _contextvars. Support objects are copied
   to unique names before being added.

Package: 78,120,379 bytes, md5 f3202fcee978102ad794360935bce44c (not yet
deployed at time of writing).

## 2026-11-10 14:42 - _ssl/_hashlib + thread-safe OpenSSL

OpenSSL 3.5.5 rebuilt in deps/openssl-3.5.5-net with sockets and threads:
  - had to drop the invalid `sock` Configure arg (sockets are on unless no-sock)
  - needed -I$DEVKITPRO/libnx/include for netdb.h/sys/socket.h
  - needed -DNO_SYS_UN_H -DOPENSSL_NO_UNIX_SOCK (Horizon has no sys/un.h)
Installed over portlibs; the old no-sock libs are kept as *.bak-nosock.
Verified: SSL_set_fd, BIO_new_socket, CRYPTO_THREAD_lock_new all present.

Python: Modules/Setup.local now also has
  _ssl _ssl.c -lssl -lcrypto
  _hashlib _hashopenssl.c -lssl -lcrypto
(_hashlib's source is _hashopenssl.c, not hashopenssl.c.) Kodi already links
portlibs/lib/libssl.a and libcrypto.a through curl, so there is exactly one
copy of libcrypto in the final binary.

Package: 78,374,331 bytes, md5 41bc86e0234a3daf061d103480513ee2.

Added script.pyprobe (C:\dev\kodi-switch\pyprobe\script.pyprobe): a normal
xbmc.python.script add-on that writes sys.version, module availability,
hashlib digests, sqlite round-trip, ssl version and os.urandom to
/switch/kodi/pyprobe.log. Trigger it over JSON-RPC with
  Addons.ExecuteAddon {"addonid": "script.pyprobe"}
so the runtime can be verified without exiting Kodi.

## 2026-11-10 14:50 - ROOT CAUSE of "script not found": fcntl/ENOTSOCK

Every Python script failed with:
  CPythonInvoker: /switch/kodi/addons/<x>/main.py not found!
  OSError: [Errno 108] Socket operation on non-socket

The file exists. The errno is the clue: 108 is ENOTSOCK (confirmed in
devkitA64 sys/errno.h).

Py_fopen() (Python/fileutils.c) does fopen(), then calls
set_inheritable(fileno(f), 0, 1, NULL) using fcntl(F_SETFD). libnx's fcntl()
only understands sockets and returns ENOTSOCK for ordinary file descriptors,
so Py_fopen discarded the successfully opened FILE*, returned NULL and set
OSError(108). Kodi prints that as "not found". io.open()/open() go through the
same _Py_set_inheritable() call, so ALL Python file I/O was broken, not just
add-on loading.

Fix: patches/python-switch-inheritable.py makes set_inheritable() a no-op on
__SWITCH__ (Horizon has no exec(), so inheritance is meaningless). Applied
before the first check that calls fcntl.

This is very likely the last blocker between "interpreter runs" and "add-ons
actually work".

Diagnostics: script.pyprobe now also serves its own results as a one-page HTTP
response on port 9998 for 15 minutes, so probe output can be read from the host
while Kodi is still running (no ftpd, no exit).

## 2026-11-10 14:57 - Python is fully working; TLS was an OpenSSL RNG bug

Probe results (script.pyprobe, served over HTTP on port 9998 while Kodi runs):
  python=3.14.6, platform=switch-64bit
  hashlib: blake2b/s blake2s md5 sha1 sha224/256/384/512 sha3_* shake_* sm3
  sqlite 3.50.4, round-trip OK
  zlib 1.3.1, bz2 OK, os.urandom OK, tempfile=/switch/kodi/temp
  socket() OK (fd=29), getaddrinfo OK, raw HTTP GET to example.com OK
  file I/O OK (probe writes pyprobe.log and serves it)
So: files, sockets, DNS, SQL, hashing, threading all work.

Remaining failure: ssl.create_default_context() ->
  SSLError: [SSL] unknown error (0xa080024)

Decoded: SSL_CTX_new() fails in ssl_lib.c because RAND_priv_bytes_ex() returns
<= 0 and OpenSSL raises ERR_R_RAND_LIB (reason 36). Horizon has no /dev/urandom
and no getrandom(), so OpenSSL's entropy pool was always empty - the same class
of bug as CPython's. This also means any HTTPS through Kodi's curl was broken.

Fix: patches/openssl-switch-rand.py inserts a __SWITCH__ branch at the top of
EVERY ossl_pool_acquire_entropy() definition in
providers/implementations/rands/seeding/rand_unix.c (there are two, behind
different OPENSSL_RAND_SEED_* guards; the first patch only hit the inactive
one). It fills the pool from libnx randomGet().
Verified: libcrypto.a now has an undefined randomGet.

Package: 78,374,331 bytes, md5 6cc50a4866e7f374bcfc2d913e358a0b.

## 2026-11-10 15:32 - HTTPS in Kodi, repo works, self-update, expat

Kodi's curl had no trust store: PreloadCaCertsBlob() is only called on Win10,
and curl was built --without-ca-bundle/--without-ca-path. Added the same call to
CPlatformSwitch::InitStageOne(). Now:
  xbmcvfs.exists('https://example.com/') == True
  UpdateAddonRepos downloads the full repo index (112 python modules listed)
  mirrors.kodi.tv addon zips are reachable

Self-update (no ftpd needed):
  python -m http.server 8002 --directory C:/dev/kodi-switch/http
  script.pyprobe console: download kodi.nro from http://PC:8002, verify md5,
  os.replace() over /switch/kodi/kodi.nro. Verified working with a 78 MB NRO
  while Kodi was running.

script.pyprobe is now a dev console served on port 9998:
  /              probe results
  /exec?code=    run a Python snippet (single quotes only - PowerShell strips " )
  /log?n=        DOES NOT WORK: Horizon returns EIO when opening a file Kodi
                 holds open for writing (kodi.log, boot.log). Logs still need
                 Kodi to exit.

XML: pyexpat/_elementtree were skipped for lack of libexpat, but CPython bundles
expat in Modules/expat. Re-enabled both; their objects also need splicing into
libpython (Modules/expat/*.o), now included in wsl-fix-python-hacl.sh.
Package 78,611,899 bytes, md5 66d75bc03bfe0e05765002f2468f536b.

Open issue: installing an add-on from the repo. InstallAddon builtin prompts
(user confirmed "Yes") but nothing installs; log not yet read. GUI install path
should be checked next.

## 2026-11-10 15:37 - add-on installs failed: missing CP437 in libiconv

Log from the failed install:
  customConvert: iconv_open() for "CP437" -> "UTF-8" failed, errno = 22
  CAddonInstallJob[script.module.requests]: invalid package
  special://home/addons/packages/script.module.requests-2.31.0.zip

Every add-on zip failed the same way, so it was never the network: the zips
download fine (CurlFile::Open succeeds, mirror redirects resolve, hashes match).

ZipManager.cpp converts each entry name from CP437 to UTF-8 unless the zip entry
has the UTF-8 flag. Our libiconv was built without the DOS encoding tables
(encodings_dos.def, which starts at CP437), so iconv_open() fails; customConvert
clears the destination on failure, the name becomes empty and the package is
rejected as invalid.

Tried rebuilding libiconv with --enable-extra-encodings: hits a devkitA64 newlib
header bug (sys/_types.h: unknown type name wint_t), so that path is parked. The
previous libiconv was restored from /mnt/c/dev/kodi-switch/portlibs.

Fix taken: overlay/xbmc/filesystem/ZipManager.cpp falls back to the raw entry
name when the conversion fails. CP437 and ASCII agree below 0x80 and add-on
entry names are ASCII, so this is safe and it needs no new dependency.

Also learned: Gui Addon install prompts ("Failed to install" generic); the real
reason is only in kodi.log. InstallAddon builtin does prompt and does attempt.

## 2026-11-10 15:41 - CP437 fallback was necessary but not sufficient

After the ZipManager fallback the install gets further but still ends with
"CAddonInstallJob[x]: invalid package". The log still shows the iconv warning
(21 times, one per zip entry) because customConvert logs before returning false;
the fallback then keeps the raw name, so those names are fine now.

The failing check is AddonInstaller.cpp:806-814:
  CDirectory::GetDirectory("zip://<package>/") must return exactly one folder,
  and LoadAddonDescription() on it must succeed.
So the zip is downloaded and hashed correctly (hash check passed, db.AddPackage
ran) but reading addon.xml out of the zip fails. Next step: test the zip VFS
directly from the console (xbmcvfs.exists/listdir/read on zip://...).

## 2026-11-10 15:55 - hbloader crash: self-update wrote a torn NRO

Atmosphere crash 2168-0001 (0x2a8), Program 010000000000100d = **hbloader**,
Exception "Instruction Abort" at 0x687e894b5c. The process that died was the
homebrew loader, not Kodi, so the NRO itself was bad.

Cause: the Python-based self-update wrote the 78 MB NRO through Python, then
renamed it into place without fsync. After the launch attempt the card still
contained the PREVIOUS build's md5 (3179815b), i.e. the FAT update was never
durable and hbloader read a torn file.

=> Python self-update is NOT safe for the NRO on FAT. Do not use it again for
   kodi.nro. Use FTP with full-file verification (wsl-deploy-nro-verify.sh,
   which uploads to a temp name, downloads the whole file back, compares md5,
   and only then swaps). Header-only checks are insufficient.

Also confirmed: the card was NOT left broken. It reverted to the previous good
build (zip-fix build 3179815b), which boots.

Dev NROs renamed to dev-*.nro.disabled so hbmenu shows only kodi.nro.

## 2026-11-10 16:00 - crash during install: stdio log flush, not the installer

Install attempt with the instrumented build did not even reach the check; kodi.log
stops at CurlFile::Open of the add-on zip. The crash report (2168-0002, data
abort at 0x30) is symbolised inside our own NRO:

  PC  _lseek_r + 0x40
  <- __swrite + 0x44
  <- __sflush_r <- _fflush_r
  <- spdlog::sinks::basic_file_sink::flush_
  <- spdlog::logger::log_it_ <- CLog::FormatAndLogInternal

i.e. Kodi died flushing kodi.log, not inside the add-on installer. The build was
running with <loglevel>1</loglevel> (debug), which hammers that path.

Actions taken:
- removed overlay/xbmc/addons/AddonInstaller.cpp (instrumentation reverted)
- removed .kodi/userdata/advancedsettings.xml (back to default logging)
- rebuilding a clean package

Still open (unchanged): installing an add-on from the repo reports
"invalid package"; xbmcvfs.listdir('zip://...') returns nothing even for a zip
we create ourselves, so Kodi's zip VFS looks broken on this platform. Next
attempt should test CZipDirectory/Directorize with a tiny zip and a log line in
CZipDirectory::GetDirectory (entry count in/out), not in the installer.

NOTE: never re-enable the Python self-update path for kodi.nro (torn writes,
hbloader instruction abort). Use wsl-deploy-nro-verify.sh.

## 2026-11-10 16:02 - overlay removals do not revert the Kodi tree

wsl-apply-overlay.sh only copies files INTO the Kodi tree. Deleting a file from
overlay/ does not restore the upstream version, so the first "clean" rebuild
still contained the AddonInstaller instrumentation (identical md5 gave it away).
Revert with:
  cd <project-dir>/kodi && git checkout -- <path>
The Kodi tree IS a git checkout; `git status --short` shows every overlay file
as modified, which is a handy way to audit what the overlay is changing.

Clean package restored: md5 3179815bfa0b78fb895953aea681649c (the zip-fix build
that boots). Instrumentation gone, advancedsettings.xml (debug logging) removed.

## 2026-11-10 16:40 - add-ons load: side-loader + The Crew installed

Kodi's built-in installer crashes the app on this port (switching arrow ->
crash). Dropped as a path; it is a real defect to revisit later.

Working alternative: /switch/kodi/temp/sideload.py, driven from the
script.pyprobe console (/exec). It searches several repositories
(OFFICIAL piers + thecrewwh/zips matrix + Gujal00 smrzips + unhingedthemes +
OldManJax + dobbelina), resolves dependencies, downloads the zips and unpacks
them into /switch/kodi/.kodi/addons. Kodi then picks them up via
UpdateLocalAddons, and enabling is done over JSON-RPC SetAddonEnabled.

Verified: script.module.requests 2.31.0 + deps -> requests works inside Kodi,
HTTPS GET to example.com and to mirrors.kodi.tv both 200.

Verified: plugin.video.thecrew 3.2.2 installed with 24 add-ons total
(script.module.thecrew 3.2.28, resolveurl 5.1.213, resolveurl.xxx, soupsieve,
beautifulsoup4, six, kodi-six, pyqrcode, cloudrequest, dateutil, twitch,
clouddrive.common, plugin.googledrive, inputstreamhelper, artwork 42 MB, ...).
Addons.GetAddonDetails -> installed=true enabled=true broken=false.
xbmc.python provided by the port is 3.1.0 (abi 3.0.0) so matrix-era add-ons
requiring 3.0.1 are satisfied.

IMPORTANT LIMIT: Horizon has no dlopen, so binary add-ons (.so) cannot load.
inputstream.adaptive is skipped by the loader (404, it has no generic zip) and
would not load even if built, unless it is linked statically into kodi.bin.
Python add-ons are unaffected. Plain HLS/MP4 still plays through FFmpeg.

Tooling note: switch_trace() now opens/closes boot.log per message so the
console can read diagnostics from inside a running Kodi. Shell commands are
truncated around 2 KB, so large payloads are pushed as base64 in two chunks and
decoded on the Switch.

## 2026-11-10 16:55 - empty add-on labels were a cold-start issue

The Crew showed 23 menu rows with blank labels. getLocalizedString() returned
"" for every id, so it looked like a PO parsing/encoding bug. Checked the file
directly over FTP: the strings.po on the card is byte-identical to the one in
the repo zip (64,998 bytes, valid UTF-8, no BOM, 708 msgid/msgstr pairs, normal
msgctxt "#32001" form). Nothing was corrupt and 17 GB was free.

Cause: the add-on had been unpacked while Kodi was already running. Kodi builds
an add-on's string table when the add-on is loaded, so a freshly side-loaded
add-on can show empty labels until the next start.

After a Kodi restart all labels load and The Crew goes much further.
=> after side-loading add-ons, always restart Kodi before judging them.

## 2026-11-10 17:30 - inputstream.adaptive built and statically linked

Horizon has no dlopen, so binary add-ons cannot be loaded at runtime. Instead:
- Built ISA's deps for aarch64-none-elf with a new CMake toolchain
  (C:\dev\kodi-switch\switch-toolchain.cmake): pugixml 1.16, nlohmann json
  3.12.0, Bento4 1.6.0-641-6-Piers -> <project-dir>/isa-deps
- Built inputstream.adaptive 22.3.24 (Piers) as a STATIC library
  (-DBUILD_SHARED_LIBS=OFF) against KodiConfig.cmake from our build tree.
  Patches needed (patches/isa-switch-*.py):
    * drop the Widevine/CDM stack (Chromium code needs dlfcn.h and platform
      macros we do not have; no DRM anyway): skip widevine subdir for
      CORE_SYSTEM_NAME=switch and guard DrmFactory with !__SWITCH__.
    * provide a local timegm() replacement (newlib has none).
- Linked into kodi.bin via ArchSetup.cmake DEPLIBS:
    inputstream.adaptive mpegts webm_parser ap4 pugixml
  (the add-on archive has no "lib" prefix, so a libinputstream.adaptive.a copy
  is needed; the TS demuxer lives in a separate lib/mpegts/libmpegts.a)
- PlatformSwitch.cpp now implements dlopen/dlsym/dlclose/dlerror: for a path
  containing "inputstream.adaptive" it returns a static handle and resolves the
  three symbols CAddonDll uses (ADDON_Create, ADDON_GetTypeVersion,
  ADDON_GetTypeMinVersion). Everything else still fails as before.
- The add-on's generated addon.xml declares library_switch="inputstream.adaptive.a",
  which is exactly the string the shim matches.

Build: kodi.bin 133 MB, package 81,126,843 bytes, md5 e67673f2d156e1556580f40da2aabddd.
Still to do: install the addon metadata dir into .kodi/addons/inputstream.adaptive
(no real .so needed), enable it, and test a DASH/HLS stream.

## 2026-11-10 17:45 - Kodi dropped the ISA add-on: no TARGET_SWITCH library branch

inputstream.adaptive was not registered at all (Addons.GetAddons type
kodi.inputstream -> 0, GetAddonDetails -> Invalid params). The addon.xml was
valid and on the card.

Cause: CAddonInfoBuilder::GetPlatformLibraryName() picks the library with an
#if chain over TARGET_ANDROID / TARGET_LINUX / TARGET_WINDOWS* / TARGET_DARWIN*.
Our build defines TARGET_POSIX + TARGET_SWITCH and none of those, so no branch
matched, libraryName stayed nullptr, m_libname was empty and
CAddonInfoBuilder::Generate() logged "addon.xml ... for binary type ... doesn't
contain library and addon becomes ignored".

Fix (overlay/xbmc/addons/addoninfo/AddonInfoBuilder.cpp):
  add a TARGET_SWITCH branch reading library_switch (first in the chain), and
  add "switch" to the supported platforms list in PlatformSupportsAddon.
The generated addon.xml already carries library_switch="inputstream.adaptive.a"
because AddonHelpers uses CORE_SYSTEM_NAME, and the port's dlopen shim matches
that string.

## 2026-11-10 18:00 - playback: local works, network does not (open issue)

State after ISA was linked and registered (installed/enabled/not broken,
v22.3.24):
- Local file playback works: /switch/bbb_720_10s_1MB.mp4 -> isPlaying within 1 s.
- Network playback does NOT start: Player.Open on an https .mp4/.m3u8/.mpd
  returns OK, Player.GetActivePlayers stays empty and the JSON-RPC worker then
  blocks (port accepts, no replies) until Kodi restarts.
- The URLs themselves are reachable from the box (CPython urllib 200 for both
  dash.akamaized.net bbb .mpd and test-streams.mux.dev .m3u8).
- xbmcvfs.exists() on https works, so Kodi's curl is fine for metadata reads.

So the fault is in the network input path used by VideoPlayer (or in how it
waits), not in DNS/TLS generally. Not yet diagnosed: needs switch_trace
instrumentation around CDVDInputStreamFile / curl open / demuxer open, plus a
plain-HTTP mp4 served from the PC.

Crash during this investigation: Atmosphere 2347-0024 (0x315B), User Break at
hbl + 0x7564, while a local video was playing and the console add-on had just
started an ad-hoc Python socket server on port 9997. Avoid running ad-hoc
servers/threads from inside the probe while media is playing; do one experiment
at a time.

## 2026-11-10 22:00 - network playback WORKS; JSON-RPC Player.Open is the broken bit

Autonomous test through netloader (no user interaction):
- nxlink launched kodi.nro; JSON-RPC came up.
- xbmc.Player().play('https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8')
  -> PLAYING after 8 s.
- xbmc.executebuiltin('PlayMedia(<same m3u8>)') -> PLAYING after 7 s.
  So the GUI/application path and the Python binding both play network HLS.
- Player.Open over JSON-RPC for the same kind of URL returns an EMPTY reply and
  wedges the RPC worker; boot.log shows no DVDInputStreamFile trace at all, so
  the player is never asked to open the input. This is a JSON-RPC-specific bug
  (probably the RPC thread waiting on an app-messenger reply), not a playback
  or network bug. It also explains why earlier "Player.Open" experiments looked
  like playback was broken.
- DASH (.mpd) via PlayMedia did not start. Note Kodi may not auto-select
  inputstream.adaptive for a bare .mpd URL; ISA selection is usually driven by
  the add-on setting the "inputstream" property. Not yet conclusive.

Instrumentation: DVDInputStreamFile::Open/Read now switch_trace url / OK /
FAILED / first read, so the next run shows exactly what the player opened.

Exit: XBPython::~XBPython() no longer calls Py_Finalize() on Switch - it waits on
Python threads and left the console stuck at "Stopping all". Built as
md5 cd5cc1ce4aa73afdae7536f4a003f62c (not deployed yet: netloader is one-shot
and ftpd needs the console).

Console note: script.pyprobe's HTTP server dies when playback starts (separate
invoker/interpreter lifecycle) and after its 20 min timeout; restart it with
Addons.ExecuteAddon (sometimes needs several tries right after boot).

## 2026-11-10 22:20 - dock/undock and touchscreen support

WinSystemSwitch:
- ctor records the current mode (appletGetOperationMode).
- MessagePump() calls HandleDisplayModeChange() every frame; on a change it
  rebuilds RES_DESKTOP at 1280x720 (handheld) or 1920x1080 (docked), re-applies
  overscan/calibrations, calls SetVideoResolution(RES_DESKTOP, true) (which
  recreates the EGL surface through CreateNewWindow and rebuilds the GUI) and
  notifies every registered IDispResource via OnResetDevice().
- Traces "display mode -> docked/handheld (WxH)" and "display mode applied".

WinEventsSwitch:
- hidInitializeTouchScreen() at construction.
- PumpTouch() per message pump: maps the first touch point to Kodi mouse events
  (XBMC_MOUSEMOTION on move, XBMC_MOUSEBUTTONDOWN on first contact,
  XBMC_MOUSEBUTTONUP on release). Touch coordinates are native 1280x720, which
  matches the handheld window 1:1. In docked mode there is no panel, so the
  code just releases any held "button".
- MessagePump returns true when either touch or a button was handled.

Still untested on hardware: needs a build+deploy with the console present.

## 2026-11-10 22:10 - packaged build waiting for deploy

Package: 81,126,843 bytes, md5 cc3c181da77c34529d10bd3daef34368
Contains: XBPython shutdown without Py_Finalize (exit fix), dock/undock
resolution switching, touchscreen-to-mouse mapping, DVDInputStreamFile traces.

Deploy needs the console (netloader Y press or ftpd). When back, the test order
that gives the most information per cycle:
 1. Exit test: power menu -> Exit. Expect return to hbmenu, no hang.
 2. Dock test: start handheld, open a menu, dock, then undock. Expect
    "display mode -> docked (1920x1080)" / "handheld (1280x720)" in boot.log,
    GUI rebuilt at the new size, video still playing across the switch.
 3. Touch test: handheld only. Tap to click, drag to scroll a list. Expect
    mouse events (no trace, but the GUI reacts).
 4. Playback test: start a Crew source. HLS should play; if a DASH/ISA source is
    chosen, check whether ISA is picked (kodi.log "inputstream.adaptive").
 5. JSON-RPC Player.Open test (for tooling): open a LOCAL file over JSON-RPC.
    If that also wedges the RPC worker while playback from the GUI works, the
    bug is in JSON-RPC/TCPServer, not in playback.

## 2026-11-11 06:40 - ISA works; second playback crashed in CPlayListPlayer

inputstream.adaptive is now proven working end to end. With a ListItem that
sets the inputstream property (the way add-ons do it):
  xbmcgui.ListItem(path=mpd) + setProperty('inputstream','inputstream.adaptive')
  -> "ISA PLAYING after 6 s", getPlayingFile() returns the .mpd
So the statically linked add-on really does load through the dlopen shim and
decode DASH.

But playing a SECOND item after stopping the first crashed Kodi
(2168-0002, data abort at null+0x560). Crash report symbolises it:
  CPlayListPlayer::OnApplicationMessage  (kodi + 0xafa178)
The code is:
    CPlayList& playlist{GetPlaylist(m_iCurrentPlayList)};
    if (!URIUtils::IsStack(playlist[m_iCurrentSong]->GetDynPath()))
      Reset();
After a single item has been stopped, m_iCurrentPlayList is still set while the
playlist/index are stale, so playlist[m_iCurrentSong] is null and ->GetDynPath()
faults.

Fix in overlay/xbmc/PlayListPlayer.cpp: bounds-check the index first
(playlist.size() > 0 && m_iCurrentSong >= 0 && m_iCurrentSong < size) and Reset()
when it is not valid. NOTE: CPlayList exposes only size(); it has no Size() and
no empty().

Also confirmed: JSON-RPC Player.Open with a LOCAL file returns OK, while the same
call with an https/HLS path blocks the RPC worker (PlayMedia with the same URL is
fine). That is a tooling-only issue; use PlayMedia from the console.

## 2026-11-11 07:05 - playlist crash fixed and verified; fast exit added

Playlist fix verified on hardware: three consecutive DASH play/stop cycles with
inputstream.adaptive, all started (6 s / 1 s / 2 s) and Kodi survived.

Exit: boot.log showed "XBMC_Run returned" ZERO times across five runs, so the
process never leaves the main loop - the crash is inside Kodi's shutdown, before
main regains control. Rather than chase every teardown subsystem, TMSG_QUIT on
Switch now saves settings and calls std::_Exit(EXITCODE_QUIT) immediately, so
the applet returns to hbmenu instead of dying with a black screen and leaving
"unable to start software" behind.

This skips database/texture-cache flushing on quit; SQLite journals make that
safe, and settings are saved explicitly first.

## 2026-11-11 07:25 - the real crash: spdlog stdio flush (not the playlist)

The repeat-play crash report symbolises to:
  PC  _lseek_r + 0x40
  <- __swrite <- __sflush_r <- _fflush_r
  <- spdlog basic_file_sink::flush_ <- dist_sink <- logger::flush_
  <- CLog::FormatAndLogInternal
Data abort at null+0x30. Same signature as the crash seen earlier when the
instrumented installer logged. It is a newlib stdio/TLS fault on the log flush
path, not the playlist (that null-deref was real and is fixed separately, but it
was not what killed these runs).

Kodi flushes on EVERY message (spdlog::flush_on(debug)), so this path runs
constantly. Mitigation in overlay/xbmc/utils/log.cpp: on Switch use
spdlog::flush_every(2s) instead, cutting the number of flushes by orders of
magnitude while still persisting logs within seconds.

If it still recurs, the robust fix is a custom spdlog sink that writes with
::write() to a raw fd, bypassing newlib stdio entirely for logging.

## 2026-11-11 07:45 - std::_Exit is NOT a valid way to quit an NRO

The fast-exit patch (save settings + std::_Exit in TMSG_QUIT) produced Horizon
error 2349-0008 (0x115d) with every register zero and PC 0: hbloader treats an
abrupt process kill as an abnormal termination. NROs must return through libnx's
normal exit path, so _Exit cannot be used.

Reverted: overlay/xbmc/application/ApplicationMessageHandling.cpp removed and
the Kodi tree file restored with git checkout.

Normal teardown is being retried now that the spdlog-per-message flush bug is
fixed - that flush was very likely what crashed the black-screen exit all along.
If teardown still crashes, add switch_trace markers through CApplication::Stop to
find the exact stage and skip only that stage (never kill the process).

## 2026-11-11 08:00 - exit crash bisected to CApplicationMessenger::Cleanup()

Added switch_trace markers through CApplication::Stop. A quit on hardware
reached, in order:
  Stop: enter / player closed / app port closed / quit announced /
  screensaver woken / alarm stopped / settings and skin saved /
  app messenger stopped / jobs cancelled
and then died in the next statement:
  CServiceBroker::GetAppMessenger()->Cleanup();

Cleanup() walks m_vecMessages / m_vecWindowMessages, signals each waitEvent and
deletes each ThreadMessage. Crashing there implies a stale or already-freed
message in the queue. Freeing them is pointless while the process is ending, so
on Switch that call is now skipped (traced as "cleanup skipped") and shutdown
continues to network-down / services / audio.

If the next run dies later, the same marker technique pinpoints the stage; the
markers are cheap and can stay.

## 2026-11-11 09:00 - exit: imperfect but non-fatal

Progression on the exit path:
- Earlier: black screen then "unable to start software" (hard crash in teardown).
- Added markers, bisected two real crash sites: CApplicationMessenger::Cleanup()
  (stale queued messages) and the job-cancel calls.
- Now Stop() returns early right after settings/skin are saved, and Run() skips
  CApplication::Cleanup() entirely on Switch, so the process ends in ~4 s.
- Result: the applet closes but hbloader reports "unable to load software"; the
  screen keeps the last frame (pointer + debug overlay) until the message is
  dismissed with OK, then the console returns home normally. No Atmosph�re fatal.

std::_Exit is NOT an option: it produced 2349-0008 with all registers zero.
So an entirely clean exit needs the remaining teardown to stop faulting, or a
proper libnx shutdown hook - deferred, since it no longer blocks anything.

Everything else on the test plan is ready: docking, touch, add-on installation
(the zip validation passes), HLS and ISA/DASH playback, repeat play/stop.

## 2026-11-11 09:20 - network throughput: libnx default socket buffers

Measured on hardware:
- 10 MB from speed.cloudflare.com via CPython urllib: did not finish in 200 s.
- 10 MB from the PC over plain LAN HTTP: did not finish in 150 s.
- The PC serves the same file locally at 233 MB/s, so the bottleneck is the
  console, not the server or the internet.
- nxlink uploads at roughly 1 MB/s, so the Wi-Fi itself is fine.

Cause: switch_preinit() used socketInitializeDefault(), which uses libnx's tiny
default TCP buffers (8 KB), throttling every transfer to tens of KB/s. That also
explains the GUI "spinner forever" on install-from-zip and the 5 minute skin
download.

Fix: socketInitialize() with explicit sizing (tx 0x8000, rx 0x10000, max
0x40000/0x80000, sb_efficiency 4, 4 bsd sessions), traced as
"socketInitialize rc=... tx=... rx=..." so it can be confirmed from boot.log.

## 2026-11-11 10:45 - throughput: it is OUR process, not the hardware

Measured, same console, same Wi-Fi, same PC server:
  nxlink (hbmenu process, 81 MB NRO):      3,047 KB/s
  Kodi Python socket, default bsd config:     36 KB/s
  Kodi Python socket, SO_RCVBUF=256 KB:      120 KB/s
  Kodi xbmcvfs (C++ curl), default:           14 KB/s
CPU while idle: 1020 MHz, CPU-KODI 0.00%. PC pulls 15 MB/s from the same CDN.
Ping to the console: min 2 ms, avg 77 ms. So: link and CPU are fine, the limit is
inside our process's BSD socket path.

Ruled out:
- custom SocketInitConfig: defaults are WORSE (36 vs 63 KB/s); all variants slow
- clock throttling (1020 MHz is stock; sys-clk installed but inactive)
- server chunking (a 1 MB-block NoDelay server is equally slow)
Python and C++ paths are both slow, so it is process-wide, not the Python bindings.

SO_RCVBUF=256 KB triples throughput, so window size matters but is not the whole
story. Remaining suspects: libnx bsd buffer pooling (sb_efficiency), the bsd
shared-memory event being starved, and our per-frame appletGetOperationMode()
IPC call in HandleDisplayModeChange() competing with the network service.

Next steps to try (in order):
 1. Throttle appletGetOperationMode() to once per second (or use the applet
    message) - it currently runs every frame and may be hammering IPC.
 2. Set CURLOPT_SOCKOPTFUNCTION in CurlFile.cpp to raise SO_RCVBUF for every
    curl socket (3x measured on a raw socket).
 3. Investigate libnx bsd session/buffer configuration with a standalone test
    NRO that downloads the same file, to separate Kodi from libnx.

## 2026-11-11 11:00 - standalone NRO gets 2.2 MB/s: the throttle is IN KODI

Built a tiny nettest.nro (C:\dev\kodi-switch\nettest\nettest.c) that does nothing
but socketInitializeDefault(), connect to the PC on port 8010 and pull 1 MB:

  nettest: 1038180 bytes in 0.45 s = 2273 KB/s

Same console, same Wi-Fi, same server, same libnx defaults. So the hardware, the
access point and libnx itself are all fine. Kodi gets 13-128 KB/s in the same
conditions, i.e. the throttling is specific to our process.

What differs between nettest and Kodi:
- Kodi's switch_preinit() runs several times, so socketInitializeDefault() was
  called repeatedly (later calls return 0xf59 "already initialised").
- Kodi also calls nifmInitialize(NifmServiceType_User); nettest does not.
- Kodi is a huge process with many threads/services; nettest is minimal.

Next build tests the two cheap differences together: socket init guarded by a
static bool (exactly once) and nifmInitialize removed entirely.

## 2026-11-11 11:10 - throughput: 4x from window size, then a hard ~130 KB/s cap

Measured inside Kodi against the same 1 MB server:
  SO_RCVBUF 128 KB ->  84 KB/s
  SO_RCVBUF 512 KB -> 136 KB/s   (best)
  SO_RCVBUF   1 MB -> 128 KB/s
  SO_RCVBUF   4 MB -> fails, errno 105 (no buffer space available)
Default (no sockopt) is 35 KB/s. So raising the receive window gives ~4x and then
plateaus at ~130 KB/s.

Ruled out so far: link (nettest 2.2 MB/s, nxlink 3 MB/s), CPU clock (1020 MHz,
0% load), server chunking, direction (console as client and as server both slow),
nifmInitialize, repeated socketInitializeDefault, custom SocketInitConfig.

The cap is specific to the Kodi process. Remaining hypotheses for next session:
- libnx bsd buffer pool allocation inside a very large process (bsd buffers are
  mapped shared memory; a big process may get a poor mapping).
- Kodi's thread priorities starving the libnx bsd copy on recv.
- Something in Kodi's startup changing socket parameters after init.

Current build restores nifmInitialize and keeps socket init guarded to run once.

## 2026-11-11 11:30 - MKV crash: HEVC, not a player bug

User's test file: /switch/One Pace S18E20.mkv, 1,235,762,281 bytes.
Header: V_MPEGH/ISO/HEVC + A_AAC (~7 Mbps, 1080p HEVC).

Symptom: plays for a moment, then frame drops accumulate over ~20 s and the
process dies (Atmosphere 2168-0002, data abort in _close_r at teardown).

Cause: the port decodes with FFmpeg on the CPU. A Tegra X1 at 1020 MHz cannot
software-decode 1080p HEVC in real time, so the pipeline falls behind and then
fails. NOT a renderer/audio bug: /switch/bbb_720_10s_1MB.mp4 (H.264) starts in
1 s and plays with a correct timecode.

Paths forward:
- short term: use H.264 sources (The Crew offers them; most add-ons resolve both)
- long term: hardware decode. Tegra X1 NVDEC supports H.264 and 8-bit HEVC, but
  wiring it into Kodi's VideoPlayer (decoder registration + surface handling) is
  a substantial project, not a tuning fix.

## 2026-11-11 12:00 - playback guard: refuse what we cannot decode, stop what starves

New: SwitchPlaybackGuard() in Application.cpp (overlay), called once a second from
CApplication::FrameMove().

1. Known impossible (software decode only, i.e. always on this port):
   - anything above 1920x1080 (4K)
   - HEVC/H.265, AV1 or VP9 above 1280x720
   These are refused as soon as the stream info is known: KaiToast
   "Can't play this video / <CODEC> at WxH decodes slower than real time...",
   then TMSG_MEDIA_STOP. No more 20 s slide into a data abort.

2. Starved decoder backstop: after 6 s of playback, if the playback clock is
   still advancing (>300 ms/s) but CDataCacheCore::GetVideoQueueLevel() stays
   at 0-1 for 8 consecutive seconds, playback is stopped with a toast.
   A frozen clock is treated as network buffering and ignored, so slow HLS
   sources are not killed by mistake.

1080p H.264 is deliberately still allowed - the guard only refuses codecs the
Tegra X1 provably cannot decode in real time.

Diagnostics: kodi.log gets "SwitchPlaybackGuard: refusing hevc (1920x1080)" or
"decoder starved", and boot.log gets a matching switch_trace line.

## 2026-11-11 12:10 - CORRECTION: that crash report is the ADD-ON INSTALLER, not video

Read the full stack of the newest report (01794411036_0100dd400c468000.log, written
~09:31 console clock, ~2 h behind the PC):

  CPosixFile::Close + 0x20
  CFile::Close
  CFile::Copy + 0xa38
  CFileOperationJob::CFileOperation::ExecuteOperation
  CFileOperationJob::DoWork
  CFilesystemInstaller::InstallToFilesystem
  CAddonInstaller::DoInstall / InstallOrUpdate / InstallModal
  CGUIWindowAddonBrowser::SelectAddonID
  ... CGUIControlButtonSetting::OnClick ... CInputManager::OnKeyUp
  CWinEventsSwitch::MessagePump

i.e. the "Install from zip spins forever" path crashed in newlib _close_r, on the
application thread, while copying files out of the package. It is NOT the video
player, and every crash report on the card stops at this one - there is no report
from the MKV playback at all.

Consequence: the MKV playback death produced no Atmosph�re report. A process that
is killed without a fault report is consistent with resource exhaustion (memory /
handles) rather than a data abort. The screenshot the user sent is this installer
report (same LR, _close_r + 0x5c), which was the newest one on the card.

So there are two separate open bugs:
 1. video: 1080p HEVC falls behind (no report) -> SwitchPlaybackGuard added.
 2. add-on install from GUI: crashes in close() while copying package files.
    Likely fd/handle exhaustion in the newlib handle table, or a double close.
    Reproduce on the current build to get a report whose offsets match the binary,
    then fix CPosixFile::Close and/or the installer copy path.

## 2026-11-11 12:25 - playback guard VERIFIED on hardware (build e0090ddf)

/switch/One Pace S18E20.mkv (1080p HEVC):
  toast shown: "HEVC at 1920x1080 decodes slower than real time on this device.
  Use an H.264 source."
  playback stopped after 2 s, Kodi kept running, no crash report.
  boot.log: "playback guard: unsupported video, stopping"

/switch/bbb_720_10s_1MB.mp4 (720p H.264):
  plays normally, timecode advances 0.1 -> 7.0 over 8 s, guard never trips.

So the HEVC failure mode is now a message and a clean stop instead of a dead
console. 1080p H.264 and 720p HEVC are still allowed to try; the queue-level
backstop stops them if they actually fall behind (>=6 s in, clock advancing,
video queue 0-1 for 8 consecutive seconds).

## 2026-11-11 12:40 - THROUGHPUT SOLVED: it was the receive window (13x -> 70x)

The 1 Hz tick was real and measurable: a blocking recv() returned one 64 KB block
every 1.000 s. 64 KB is exactly libnx's default tcp_rx_buf_size.

Measured bsd defaults (logged from the console):
  tx=32768  rx=65536  txmax=262144  rxmax=262144  sb=4  sessions=3  type=bsd:u

Fix 1 - explicit SocketInitConfig in switch_preinit() (build b334ca14):
  tcp_rx_buf_size = 0x40000 (256 KB, fixed)
  tcp_tx_buf_size = 0x10000
  sb_efficiency   = 8
  num_bsd_sessions= 6
  -> socketInitialize rc=0x0

Result on the same 1 MB test:
  before: 64 KB blocks, 1.000 s apart  ->   64 KB/s
  after : ~190 KB blocks, 0.315 s apart ->  861 KB/s (raw socket, 4 MB run: 780 KB/s)
  xbmcvfs/curl: 14 -> 188 KB/s

Fix 2 - per-socket SO_RCVBUF (measured, not yet built):
  no sockopt        :  482 KB/s
  SO_RCVBUF=256 KB  : 2065 KB/s
  SO_RCVBUF=512 KB  : 2682 KB/s   <- matches the standalone NRO (2.2 MB/s)
So the bsd pool alone is not enough; the per-socket window must be raised too.
curl never got it in practice (xbmcvfs stayed at 188 KB/s), which is why
CCurlFile::SetCommonOptions now forces SO_RCVBUF=512 KB via CURLOPT_SOCKOPTFUNCTION,
raises CURLOPT_BUFFERSIZE to 512 KB, and traces once when the callback fires so we
can confirm it is actually invoked.

Plain http.client from the console against the same server: 536 KB/s, so the PC
test server itself is now the next ceiling to watch, not the Switch.

IMPORTANT for future tests: the PC port 8010 server used to be single-threaded
PowerShell; it is now C:\dev\kodi-switch\netsrv.py (threaded, accepts 32). The old
one deadlocked every parallel-socket test.

## 2026-11-11 13:20 - why ISA never loaded: devkitPro's dlfcn.h has INLINE stubs

kodi.log said it plainly once debug logging was on:

  ADDON: Could not locate inputstream.adaptive.a      (file was missing -> created it)
  Unable to load .../inputstream.adaptive.a, reason: dynamic loading unsupported

The second message is not ours. <project-dir>/portlibs/include/dlfcn.h
(devkitPro portlibs, on the include path) defines:

  static inline void *dlopen(const char *f, int flags) { return 0; }
  static inline char *dlerror(void) { return (char *)"dynamic loading unsupported"; }

So every translation unit that includes <dlfcn.h> - including Kodi's SoLoader.cpp -
bound its calls to the inline stub. PlatformSwitch.cpp's real dlopen/dlsym shim
existed in the binary but was never called. ISA therefore never loaded, which also
explains the earlier "ISA selected but playback stops" mystery.

Fix (overlay): SoLoader.cpp now declares switch_dlopen/switch_dlsym/switch_dlclose/
switch_dlerror and #defines dlopen..dlerror onto them for TARGET_SWITCH;
PlatformSwitch.cpp provides those names. Bypasses the header stubs entirely, no
global rebuild of everything that includes dlfcn.h.

Also needed on the SD card: a zero-byte
  /switch/kodi/.kodi/addons/inputstream.adaptive/inputstream.adaptive.a
so Kodi's "does the library file exist" check passes before calling dlopen. This
file disappeared at some point (an add-on update or sideload rewrote the folder).

Separately, the crash during the ISA attempt was a User Break (2347-0024, abort),
not a memory fault; no symbols in the report. Hold judgement until ISA actually
loads - a discarded instance during playback is a plausible abort source.

## 2026-11-11 13:40 - ISA now loads; local HLS plays; remote HLS aborts

After the SoLoader redirect, ISA finally initialises for real:

  local HTTP HLS through ISA:  t=5.2  video=True   <- works
  remote HTTPS HLS (mux):      t=0.0  video=False  <- stalls, then aborts
  DASH live (dashif livesim):  stopped @4 s, also aborts

Two crashes so far, both Result 0x315B (2347-0024) User Break = abort(), no
symbols (the Atmosphere report lists only the "hbl" module; the NRO has no
attributed module), and kodi.log loses its last ~2 s because spdlog flushes every
2 s. The last flushed line in both cases is:
  ADDON: Dll Initializing - InputStream Adaptive
which may or may not be the last thing that actually ran.

Added (this build): std::set_terminate + SIGABRT/SIGSEGV/SIGILL/SIGFPE/SIGBUS
handlers that write the reason (and exception what()) straight to boot.log via
switch_trace before dying. That should make the next abort explainable.

Hypotheses for the abort, in order:
 1. uncaught std::runtime_error from KODI::MEMORY::AlignedMalloc under memory
    pressure (1080p remote variant allocates much more than the 720p local one)
 2. an ISA assert while parsing the remote manifest
 3. stack exhaustion in an ISA thread

Note: 1080p HEVC/AV1 remain refused by SwitchPlaybackGuard; 1080p H.264 is allowed
but is also the most memory-hungry case.

## 2026-11-11 14:00 - ISA STREAMS: HLS works over the internet, DASH does not

Build 6191d1a3. Measured on hardware through inputstream.adaptive:

  local HTTP HLS (variant, 3 segments)      video=True, t=5.2
  remote HTTPS HLS 720p variant (mux CDN)   video=True, t=11.2 over 20 s, no crash
  remote HTTPS HLS master (Apple bipbop)    video=True, t=3.2
  remote HTTPS HLS master (mux x36xhzz)     stops @4 s (provider-specific)
  DASH VOD (akamaized EnvivioDash3)         stops @4 s
  DASH live (dashif livesim)                stops @4 s

So the adaptive pipeline (ISA instance -> manifest -> representations -> segments ->
FFmpeg -> renderer) works for HLS over HTTPS. DASH fails for both VOD and live.

Crash handling added in the same build:
  std::set_terminate now logs the exception TYPE (via __cxa_current_exception_type,
  no rethrow - rethrowing inside the handler terminated again and lost the reason)
  ADDON_Create is wrapped: a C++ exception escaping ISA becomes
  ADDON_STATUS_PERMANENT_FAILURE with "ADDON_Create threw: <what>" in boot.log,
  instead of killing the process.

Next: DASH diagnosis needs kodi.log with debug on (rotates at next start). Then test
a real The Crew HLS source end to end.

## 2026-11-11 14:15 - DASH crash identified: uncaught std::system_error

boot.log from the crashed session (crash handler working):

  TERMINATE: std::terminate called
  TERMINATE: exception type St12system_error

std::system_error is thrown by std::thread (thread creation / join) and by
mutex/call_once misuse. It is escaping in a thread and terminating the process,
after the DASH open has already failed (the 4 s stop precedes the death by a bit).
The same log shows "ADDON_Create returned 0" repeatedly, so ISA instances are
being created and destroyed normally on the HLS path.

Not addressed yet. Priority is lower than it looks: DASH is already broken, and the
crash only happens on the DASH failure path. HLS (the common case) works and
survives repeated play/stop.

To diagnose further we need the throw site: log what() as well (currently only the
type is logged, because rethrowing inside the terminate handler terminated again),
or interpose __cxa_throw to capture a backtrace.

## 2026-11-11 14:40 - installs work; Python downloads patched; Crew scrapers suspect

Reported by the user:
- a second repository installed from the GUI without trouble -> the old installer
  crash (CPosixFile::Close) did not reproduce; treat it as fixed-or-transient.
- a ~200 MB build download ran slowly and was cancelled; Chef Wizard retry in
  progress.
- The Crew resolves no streams (suspected dead scrapers, which matches the wider
  state of free providers).

Add-on inventory confirms The Crew has its resolver stack:
  script.module.resolveurl, script.module.resolveurl.xxx, script.module.thecrew,
  script.module.requests/urllib3/certifi/chardet/idna, beautifulsoup4, soupsieve,
  kodi-six, cloudrequest, pycryptodome, pil, inputstreamhelper
so the failure is not a missing module.

Python socket patch installed at
  /switch/kodi/python/lib/python3.14/sitecustomize.py
It patches socket.socket.connect/connect_ex to set SO_RCVBUF=256 KB, matching what
CCurlFile does. Loads at the next Kodi start (site is imported, sitecustomize was
not present before). Measured in-process: Python http.client 574 KB/s vs Kodi
curl 1275 KB/s - Python is limited by more than the window, so this is a modest
improvement for build installers, not a cure.

## 2026-11-11 15:20 - "2.5 fps" on the idle GUI is the dirty-skip, not a slow loop

Status Monitor shows ~2.5 fps (fluctuating every few seconds) on an idle GUI and
returns to normal during video playback. Confirmed by the user that navigation is
immediately responsive. The port skips GUI redraws when no region is dirty, so the
counter (which counts presented frames) reports the redraw rate, not the loop rate.
Cosmetic. Do not "fix".

Also during this session: the chef21 wizard's build mirrors measured from the PC at
31 MB/s (diggz1.me) and 13.7 MB/s (archive.org), so slow build downloads are the
console's own ceiling (~1.3 MB/s) or the wizard's Python loop, never the mirror.
Screensaver set to None over JSON-RPC so a tap cannot cancel a download.

## 2026-11-11 16:40 - MX Build (chef21) installed; databases incompatible; ISA restored

What the wizard did: downloaded MX Build v1.7 (209 MB, diggz1.me), then did a
CLEAN install - it replaced .kodi/userdata AND removed add-ons it did not ship,
including our inputstream.adaptive.

Result: Kodi died on every start at "Connecting to database please wait"
(2345-0008, all registers zero). Cause: build databases from another Kodi version.

Recovery (build f2d51c97, temporary): switch_preinit() renamed
.kodi/userdata/Database to Database.build-broken before Kodi opened it. Kodi then
created fresh databases and started normally. That code has been REMOVED again -
the clean rebuild produced the identical binary 6191d1a3, and leaving it in would
have moved the new databases aside on the next boot.

State after recovery:
  skin               skin.pm3-hd.extended  (requires xbmc.gui 5.17 - satisfied by 22)
  add-ons            161, broken: 0
  inputstream.adapt  DELETED by the build -> rebuilt from addon.xml.in:
                     platform=switch, library_switch="inputstream.adaptive.a",
                     requires kodi.binary.instance.inputstream 3.4.0
                     (version read from /switch/kodi/addons/kodi.binary.instance.inputstream)
                     plus zero-byte placeholder .a as before.
                     Pushed in 1500-char base64 chunks (the probe's HTTP server
                     truncates request lines longer than ~4 KB; md5 verified).
                     UpdateLocalAddons registered it; ISA plays local HLS
                     (t=13.2, video=True).
  our side-loader  script.pyprobe was disabled by the clean install; re-enabled
                   over JSON-RPC.

Lessons:
  - Before any build install, back up /switch/kodi/.kodi (userdata + addons).
  - The probe's /exec request line is limited to roughly 4 KB; push files in
    <=1500-char base64 chunks and verify with md5.

## 2026-11-12 00:10 - local playback verified with synthetic 1080p material

Test files generated on the PC with a full ffmpeg (imageio-ffmpeg binary; the
HandBrake install here is GUI-only and the ffmpeg in Downloads is a crippled
--disable-everything build):

  testsrc2 + sine, 1920x1080p30, H.264 High@4.1, 10 Mbps, 40 s
  test1080.mp4      AAC stereo 192 kb/s   51,333,599 bytes
  test1080_ac3.mp4  AC-3 5.1   384 kb/s   52,291,590 bytes

Copied to /switch/ with xbmcvfs.copy over LAN: 786 KB/s and 946 KB/s (51 MB in
64 s, 52 MB in 54 s). Both play from the card:

  playback clock advances +1.02 s per wall second for 22 s -> real time
  video=True throughout, no SwitchPlaybackGuard trip (1080p H.264 is allowed)
  seek to 30 s works and playback continues (t=32.7 after 3 s)
  audio audible on both AAC stereo and AC-3 5.1

Player.HasAudio reports False for BOTH files in this build, so it is not a usable
signal for audio presence here - do not use it to diagnose audio.

Conclusion: 1080p H.264 is genuinely watchable on this port; the guard's decision
to allow 1080p H.264 and refuse 1080p HEVC is correct.

## 2026-11-12 03:20 - night findings: why https/screenshots fail, and an HEVC decoder exists

### The devkitPro ffmpeg is trimmed in two ways that matter

1. --disable-encoders --disable-muxers  -> avcodec_find_encoder(AV_CODEC_ID_PNG/MJPEG)
   returns NULL, so Kodi cannot write screenshots or thumbnails
   ("You are missing a working encoder for format: PNG" in kodi.old.log).
2. --enable-protocol=file,http,ftp,tcp,udp,rtmp,tls,httpproxy  -> note: tls is
   listed but **https is not**. In ffmpeg 7.1 https_protocol is a separate entry
   (https_protocol_select="tls_protocol"), so ffmpeg had no https at all.
   Runtime proof: libavformat/avio.c:343 warning "https protocol not found".

The libnx TLS backend does exist (tls.patch adds tls_libnx.c + --enable-libnx),
so TLS was available; only the protocol name was missing from the enable list.

### Rebuilding ffmpeg for switch (ffmpeg 7.1, matching libavcodec 61.19.100)

Source: ffmpeg-7.1.tar.xz + devkitPro's ffmpeg-7.1.patch + tls.patch.
Correct configure deltas vs the devkitPro PKGBUILD:
  + --enable-encoder=png,mjpeg      (keep --disable-encoders)
  + https in --enable-protocol
  + --pkg-config=/usr/bin/pkg-config   (cross_prefix otherwise looks for
                                        aarch64-none-elf-pkg-config and falls
                                        back to "false", failing every lib check)
  + source /opt/devkitpro/switchvars.sh, which supplies
        -isystem $DEVKITPRO/libnx/include   (netdb.h, netinet/in.h live there;
        without it every network feature check fails and network/tcp/tls/https
        all get disabled)
  - dropped libass/freetype/fribidi and bzlib (not needed by Kodi; their
    pkg-config chains are not installed here)

Resulting configure: protocols file https tls http tcp udp ftp rtmp rtp
httpproxy; encoders mjpeg png; libnx TLS yes; nvtegra yes.

### HEVC: there is a hardware decoder in this ffmpeg

DevkitPro's patch adds Tegra NVDEC decoders:
  hevc_nvtegra h264_nvtegra vp9_nvtegra vp8_nvtegra vc1_nvtegra
  mpeg1/2/4_nvtegra wmv3_nvtegra mjpeg_nvtegra
They output AV_PIX_FMT_NVTEGRA, which is AV_PIX_FMT_FLAG_HWACCEL (opaque), but
the surfaces are nvmap allocations exposed through AVNVTegraMap and
av_nvtegra_map_get_addr() returns a CPU-addressable pointer.

So a Kodi integration is plausible without full GPU interop:
  - select the *_nvtegra decoder for HEVC/H.264
  - take the NVTEGRA frame, map it (av_nvtegra_map_*), wrap as NV12
  - hand it to the existing renderer path
Still a real project (decoder selection, hw frames context lifetime, map/unmap
per frame, colour conversion), but the hard part - an NVDEC HEVC decoder that
works from a homebrew process - already exists in this ffmpeg.

## 2026-11-12 03:45 - new ffmpeg built, installed, and Kodi relinked (build 586e8c68)

ffmpeg 7.1 rebuilt for switch with:
  CONFIG_PNG_ENCODER 1
  CONFIG_MJPEG_ENCODER 1
  CONFIG_HTTPS_PROTOCOL 1
  CONFIG_TCP_PROTOCOL 1
  CONFIG_TLS_PROTOCOL 1
  CONFIG_HLS_DEMUXER 1     (ffmpeg HLS now has HTTPS behind it)
  CONFIG_DASH_DEMUXER 0    (not enabled; can add --enable-demuxer=dash later)
  nvtegra hw decoders present (hevc/h264/vp9/vp8/vc1/mpeg1/2/4/wmv3/mjpeg)

Installed to /opt/devkitpro/portlibs/switch (libs + headers). Originals are kept
as libav*.a.bak-orig; restoring them is a copy back, no rebuild of ffmpeg needed.

Kodi rebuilt and relinked against it: package md5 586e8c689694e941765aef54e36bff1d.

### Test plan for the next session (one deploy)

1. Screenshot: TakeScreenshot(/switch/kodi/temp/shot.png) must now produce a file
   (previously nothing at all). This also restores thumbnail/artwork caching.
2. ffmpeg HLS over HTTPS with NO inputstream.adaptive: play
   https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8 via plain PlayMedia.
   Before this build ffmpeg logged "https protocol not found".
3. Regression: /switch/test1080.mp4 local playback (real-time + audio),
   HLS through ISA (Apple master), and The Crew scrape->play path.
4. If a screen photo is needed, TakeScreenshot + reading the PNG over the probe
   replaces asking the user.

Rollback: copy /opt/devkitpro/portlibs/switch/lib/libav*.a.bak-orig back over the
current libs and rebuild kodi.

## 2026-11-12 06:40 - screenshots work (build 7a869e6d): I have eyes on the console

TakeScreenshot previously wrote nothing: Kodi 22 routes screenshots through
CCaptureService, and only the DRM-PRIME renderer supplies a capture provider.
LinuxRendererGLES does not, so Submit() never completed - silently.

Fix (overlay xbmc/utils/Screenshot.cpp): on TARGET_SWITCH, TakeScreenshot()
bypasses the capture service and does a synchronous framebuffer readback -
glBindFramebuffer(0), glReadPixels(RGBA), vertical flip, then
CPicture::CreateThumbnailFromSurface(). Runs on the application thread where the
GL context is current.

Verified: TakeScreenshot(/switch/kodi/temp/shot.png) -> 355,941-byte PNG in 1 s.
The PNG transfers over the probe in one response (~475 KB base64) and renders:
Estouchy skin, 1280x720, home screen, correct icons and clock.

This also confirms the rebuilt ffmpeg is linked into kodi.bin (the PNG encoder is
the only reason the write succeeds). Earlier "encoder absent" check was invalid -
it searched for a string that does not exist in pngenc.c.

Consequence: no more asking the user for photos; boot.log + screenshots cover
diagnosis.

## 2026-11-12 06:55 - ffmpeg HTTPS+HLS works (build 224779a2)

sslInitialize(2) added to switch_preinit: rc=0x0. ffmpeg's libnx TLS backend
(tls_libnx.c) needs the ssl service; Kodi's own curl/OpenSSL never did, which is
why only ffmpeg's https was broken.

Regression on 224779a2 (internet was 202 KB/s at the time - see note):

  local 1080p30 H.264 10 Mbps from card     PLAYS  t=8.2
  HLS via inputstream.adaptive (mux 720p)   PLAYS  t=5.7
  HLS via ffmpeg demuxer, NO ISA (mux 720p) PLAYS  t=16.1   <- new
  screenshot                                OK     356 KB PNG

The two earlier "stalled at t=0" results were the Apple CDN, not the build: the
same Apple URL worked earlier in the day and the console's internet had dropped to
202 KB/s during the test while the PC still pulled 31 MB/s from that CDN.

Known limits at wrap-up:
  HEVC decode      software only, too slow for 1080p (guard refuses it). ffmpeg
                   ships hevc_nvtegra (NVDEC) - integration plan in this file.
  DASH             ffmpeg's dash demuxer is disabled (needs libxml2); ISA's DASH
                   path aborts with an uncaught std::system_error on failure.
  Exit             2345-0008 error screen after close; console returns to hbmenu
                   only after POWER.
  Free sources     torrent-only in practice; debrid is the realistic route.

General Horizon/libnx/ffmpeg/Kodi knowledge extracted from this port lives in:
  <user>\OneDrive\Documents\Default Project\AGENTS.md
(auto-loaded into future agent sessions in this workspace)

## 2026-11-12 14:20 - DASH diagnosed: ENOBUFS, the bsd buffer pool

With debug logging the DASH failure is explicit:

  Open - probing detected format [dash]                    <- demuxer is present
  [tcp] Connection to tcp://dash.akamaized.net:443 failed:
        No buffer space available                           <- ENOBUFS
  [dash] Failed to open an initialization section
  [dash] Error when loading first fragment of playlist

So ffmpeg's DASH demuxer works (the MPD is parsed and the format detected), but it
opens its own sockets while Kodi's CFileCache still holds one, and the bsd buffer
pool is exhausted.

Cause: SocketInitConfig.sb_efficiency multiplies the buffer reserved per socket.
We had 256 KB rx * 8 = ~2 MB per socket; a second consumer tips it over.

Change: sb_efficiency 8 -> 2 (keep tcp_rx_buf_size 256 KB and the per-socket
SO_RCVBUF 256 KB that actually produce the throughput).

Also confirmed: the DASH demuxer and libxml2 are in the binary (SegmentTemplate,
AdaptationSet, xmlReadMemory present in the NRO).

Exit status meanwhile: ~30 s hang, then returns to the Switch HOME menu with no
error screen (previously 2345-0008 + power cycle). switch_cleanup() atexit traces
never appear in boot.log, so atexit is not running - unresolved but no longer
user-visible.

## 2026-11-12 14:45 - socket config finalised (build 4f529fbd); DASH documented

Final SocketInitConfig:
  tcp_rx_buf_size     = 0x20000 (128 KB default)
  tcp_rx_buf_max_size = 0x20000
  tcp_tx_buf_size     = 0x10000, max 0x40000
  sb_efficiency       = 4
  num_bsd_sessions    = 6
plus per-socket SO_RCVBUF = 256 KB on curl handles (CCurlFile).

Measured with this build (LAN, PC on :8002):
  xbmcvfs / curl   4780 KB/s
  Python http      4635 KB/s
i.e. 3x the 1.4 MB/s we had earlier - reducing pool contention helped throughput as
well as headroom.

Tuning lessons:
  sb_efficiency=8, rx=256K  -> high throughput but ENOBUFS once a second consumer
                               (DASH) opens its own socket
  sb_efficiency=2, rx=256K  -> the probe (a listener) fails to start at all
  sb_efficiency=4, rx=128K  -> best measured: ~4.8 MB/s and several sockets fit

DASH: still fails, and now precisely explained - ffmpeg's dash demuxer parses the
MPD (log: "probing detected format [dash]") but its own connect() returns ENOBUFS
("No buffer space available") even with the tuned pool, then "Failed to open an
initialization section". Root cause is ffmpeg opening sockets outside Kodi's cache
path; fixing it properly means giving ffmpeg a socket allocator or routing its
dash I/O through the VFS. Left as a documented limitation: HLS works via both ISA
and ffmpeg, which covers the add-ons in use.

Build of record: 4f529fbdae35eec1256ce148b3809090

## 2026-11-12 15:40 - HARDWARE HEVC WORKS (build dc5e5ee5)

User confirms: 1080p Main 10 HEVC plays with audio and video at full frame rate.

The chain that made it work:
 1. Probe proved NVDEC is reachable from Kodi's process:
    av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_NVTEGRA) -> rc=0.
 2. CDVDVideoCodecFFmpeg::Open() creates the device (HEVC only) and attaches
    hw_device_ctx; GetFormat() accepts AV_PIX_FMT_NVTEGRA and marks the decoder as
    hardware (so SwitchPlaybackGuard stops refusing 1080p HEVC).
 3. GetPicture() transfers each hw frame to a software frame. The destination
    format is taken from hwfc->sw_format (NV12 for 8-bit, P010 for 10-bit) - a
    hardcoded NV12 failed silently on Main 10.
 4. Main 10 arrives as P010, which the GLES renderer cannot display (audio only,
    then a crash). A sws_scale P010->NV12 pass inside GetPicture produces NV12,
    so the renderer sees a normal 8-bit frame. No user-side transcoding.
 5. av_frame_copy_props() preserves timestamps (A/V sync).
 6. If the hw open fails, Open() retries in software; H.264 stays on the
    software path entirely, so nothing that worked before is at risk.

Files: overlay/xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecFFmpeg.{h,cpp}

Remaining HEVC limits: only HEVC takes the hw path for now (H.264 riscv per the
safety decision). 4K HEVC untested; P010->NV12 conversion is a per-frame CPU copy.

Also fixed: Kodi's clock was 6 hours ahead because nothing set TZ, so newlib's
localtime() reported UTC. switch_preinit now sets TZ (default CST6CDT,
overridable via /switch/kodi/timezone.txt).

## 2026-11-12 16:00 - WatchNixtoons2 0.14.13 installed (no patches needed)

Source: github.com/Marvellye/plugin.video.watchnixtoons2 (0.14.13 - the "14.x" the
user runs on other devices; the Crew repo and christianhaitian forks are older).

Dependencies (script.module.requests, script.module.six, inputstream.adaptive) were
already installed. Code scan for Python 3.14 hazards found only xbmc.translatePath
in lib/common.py, and it is guarded by `if six.PY2:` - the Python 3 branch already
uses xbmcvfs.translatePath. So: no code changes at all.

Installed by packaging the repo contents (forward-slash zip, addon.xml at root),
copying over the LAN, unzipping into .kodi/addons, UpdateLocalAddons, enabling.
Files on the card: /switch/kodi/.kodi/addons/plugin.video.watchnixtoons2
Labels come up empty until the next Kodi start (add-on string tables load at
add-on load time - same as The Crew).

## 2026-11-12 16:30 - socket pool capacity measured; WatchNixtoons2 diagnosis

The bsd pool is a small fixed budget and socket count scales inversely with the
per-socket reservation. Measured on hardware:

  rx=128 KB, sb=4, sessions=12   -> only 2 sockets before ENOBUFS
  rx= 64 KB, sb=2, sessions=6    -> the probe listener cannot bind at all
  rx=128 KB, sb=4, sessions=6    -> binds and supports several sockets, ~4.8 MB/s
                                    LAN through curl (which sets its own 256 KB
                                    SO_RCVBUF)

So: keep sb_efficiency at 4 and sessions at 6; do not try to fix capacity by
raising sessions. Raising num_bsd_sessions made it worse, not better.

Symptom this caused: add-ons that make several HTTP requests in a row (menu ->
show -> embed -> JSON -> artwork) die part-way with
"OSError: [Errno 105] No buffer space available" from plain socket(), and ffmpeg's
DASH demuxer failed at connect() the same way.

WatchNixtoons2 (0.14.13) investigation:
  - installed, enabled, menus list correctly; labels appear after a restart
  - playback fails with Kodi's "Playback failed"; the log shows Kodi skipping the
    plugin URL as unplayable after the resolver ran
  - the episode page (wcostream.tv) loads with Python requests + browser UA: 200,
    192 KB, contains the "uploads0" iframe but no "getvid"
  - the embed page (embed.wcostream.com/inc/embed/index.php?file=...) also loads
    (200, ~9 KB) but contains no getvid/evid
  - the PC cannot fetch wcostream at all: PowerShell/.NET gets 403 (TLS
    fingerprint), the console's Python gets 200
  - the same embed request from the console later failed with ENOBUFS, so part of
    the failure may be socket exhaustion rather than the site changing

Next step for this add-on: re-run the resolver with a healthy socket state, and if
the embed page still lacks getvid, patch the parser for wcostream's current player
markup (the add-on has several branches for exactly this, so a new one is the
likely fix).

## 2026-11-12 17:10 - EXIT: documented as acceptable; how to exit properly

Status: exit WORKS, it is just slow and noisy. Marked as irrelevant for now.

What actually happens:
  - HOME -> power icon -> Exit (or Close) is the correct way out.
  - the app becomes unresponsive (HOME button ignored) for roughly 30 seconds
  - then Kodi closes and the console returns to the Switch HOME menu (not hbmenu)
  - there is NO Atmosphere error screen any more
  - do NOT hold POWER / pull the console during those 30 s; it is working

Why it is slow: our CApplication::Stop() returns early and Run() skips Cleanup(), so
Kodi's threads are torn down by process exit rather than in order, and the libnx
services we start (socket, ssl, nifm, hid) are never released by us. switch_cleanup()
is registered with atexit but the traces never appear in boot.log, so atexit is not
running - Horizon ends up reclaiming everything itself.

Cheap experiment for later (not urgent): call switch_cleanup() explicitly in
platform/posix/main.cpp right after XBMC_Run() returns, instead of relying on atexit.
If the 30 s is service teardown, that should shorten it.

Returning to the HOME menu rather than hbmenu is expected: hbmenu runs as an applet in
Album, and launching a full-memory application from it closes the applet; when the app
ends the system lands on HOME.

## 2026-11-12 17:25 - WatchNixtoons2: site flow solved, blocked by the socket pool

Installed 0.14.18 (the newer fork, github.com/the1andonlym/kodi-addons zip) over
0.14.13. That version implements the site's CURRENT flow, which 0.14.13 predates:

  GET  /assets/ads/advertisement.js?flag=__abd_<rand>&_=<ms>
  POST /ad-verify   {"nonce": <uuid hex>, "status": "clear", "id": <pid>}
  then rewrite the embed URL from inc/embed/index.php to inc/embed/video-js-old.php
  and append &n=<nonce>, sleep 5

So the video is no longer obtainable with the old `sources: [...]` / getvid parsing;
0.14.18 does the handshake correctly. Its failure now is ours:

  requests.exceptions.ConnectionError: ... /assets/ads/advertisement.js
  Caused by: socket.socket() -> OSError: [Errno 105] No buffer space available

i.e. the bsd socket pool runs out mid-flow. Measured behaviour of the pool:
  rx=128K,sb=4,sessions=6   binds, several sockets, ~4.8 MB/s curl   <- kept
  rx=128K,sb=4,sessions=12  only 2 sockets
  rx=128K,sb=2,sessions=6   listener cannot bind at all
  rx=32K ,sb=4,sessions=6   probe listener cannot bind at all

The pool is a fixed Horizon budget that libnx only forwards to bsd:u; it cannot be
enlarged from SocketInitConfig. So the port is back on the known-good combination
and the practical consequence is documented: add-ons that open many sockets at once
can still fail with ENOBUFS.

Next ideas if this is ever revisited (not urgent):
  - serialise the add-on's requests (patch it to reuse one requests.Session and
    close responses) so it needs fewer sockets simultaneously
  - or have Kodi's Python close idle sockets aggressively

## 2026-11-12 17:45 - WatchNixtoons2 connection-close patch prepared

The add-on's lib/network.py keeps one requests session alive with keep-alive, so
every host it talks to (wcostream, embed.wcostream, cdn) holds a socket open. On a
small bsd pool that alone can exhaust it mid-flow.

Patch prepared in the local copy and packaged as:
  C:\dev\kodi-switch\http\plugin.video.watchnixtoons2-0.14.18-patched.zip
It adds  'Connection': 'close'  to the request headers in lib/network.py, so each
response releases its socket immediately.

Applying it needs one healthy probe window (the probe's listener cannot bind once
the pool is tight, and disabling plugin.video.youtube first did not help enough on
the last attempt - the app thread then stopped answering JSON-RPC and the console
needed a reboot).

To apply next session, one of:
  a) start script.pyprobe early after a fresh boot, then copy the zip from
     http://<pc-ip>:8002/ and unzip over
     /switch/kodi/.kodi/addons/plugin.video.watchnixtoons2
  b) or install the packaged zip through Kodi's GUI "Install from zip" once a file
     source for the PC share exists

## 2026-11-12 18:05 - CRASH after ~30 s of HEVC playback over HTTP: diagnosed and fixed

Symptom: playback of One Pace S18E01 (1080p, 10-bit HEVC) over http://<pc-ip>:8003
ran for roughly half a minute, then Atmosph�re showed a crash screen. No crash report
was written (newest /atmosphere/crash_reports/*.log was 15:30, i.e. hours earlier), so
the process died without a fault report.

Evidence (all from the device, not inferred):
  /switch/kodi/temp/kodi.old.log  (the crashed run, rotated at next start)
    - 17:37:28 CDVDVideoCodecFFmpeg::Open() Using codec: HEVC   (software context)
    - 17:37:28 [AVHWFramesContext] Frame address/pitch not aligned to 256,
               falling back to cpu transfer                        <- NVDEC really decoding
    - 17:37:28 first of 511 identical failures, last at 17:37:59:
                 [src] Setting BufferSourceContext.pix_fmt to a HW format requires
                       hw_frames_ctx to be non-NULL!
                 Query format failed for 'src': Invalid argument
                 CDVDVideoCodecFFmpeg::FilterOpen - avfilter_graph_config
    - 17:37:59 Thread JobWorker terminating x4, then the log is cut mid-write
  /switch/kodi/boot.log (our own traces, open/write/close per line)
    - NVTEGRA device rc=0 / format selected (hardware decode) / P010->NV12 scaler ready
    - TERMINATE: std::terminate called   <- immediately after
  Earlier runs show the same terminate with "exception type St12system_error", i.e.
  std::system_error (thread/resource creation) escalating to std::terminate.

Cause: our NVDEC path leaves avctx->pix_fmt = AV_PIX_FMT_NVTEGRA. Kodi's *software*
decoder path (the one used when decoding happens in a codec context rather than in a
m_pHardware wrapper) decides whether it needs a filter graph with
    need_scale = m_formats.find(m_pCodecContext->pix_fmt) == end
and FilterOpen() builds its buffer source from that same pix_fmt. NVTEGRA is a hardware
format, so buffersrc refuses it ("requires hw_frames_ctx"), FilterOpen fails, FilterClose
frees it, and the next frame repeats the whole cycle - 511 times in 31 s, each cycle
allocating an AVFilterGraph plus fresh 1080p frame buffers. That churn ends in
std::system_error -> std::terminate.

Second, quieter mismatch: avctx->sw_pix_fmt stays P010 for Main 10, and
GetPictureCommon() copies sw_pix_fmt into VideoPicture::pixelFormat - so Kodi was told
"P010" while being handed NV12 pixels.

Fix (overlay/xbmc/cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodecFFmpeg.cpp, in GetPicture
right after a successful transfer):
    m_pCodecContext->pix_fmt    = AV_PIX_FMT_NV12;
    m_pCodecContext->sw_pix_fmt = AV_PIX_FMT_NV12;
Both are set only after the frame has really been transferred, so a failed transfer still
reports the truth. With NV12 in m_formats, need_scale is false, no filter graph is built
at all, and the per-frame open/fail/free cycle disappears.

Test to run: same file over HTTP for several minutes; expect zero "FilterOpen" lines in
kodi.log and no crash.

Side note: the crashed run had Kodi debug logging on, and our RenderManager RMDBG lines
fire every frame. Useful while chasing this, expensive in normal use - turn debug off
once the HEVC path is settled.

## 2026-11-12 18:20 - Startup socket consumers, The Crew, nixtoons patch

The Crew's health-check notification is a SERVICE, not a plugin quirk:
  Addons.GetAddons type=xbmc.service lists plugin.video.thecrew v3.2.2 enabled=True
i.e. the plugin also declares <extension point="xbmc.service">, so it runs on every Kodi
start and posts the notification. Disabling the add-on (Addons.SetAddonEnabled false)
stops both the notification and the service thread. It is off right now.

Note on the general behaviour (user observation, matches our measurements): add-ons that
hold sockets open at startup cost a few seconds on desktop, but on Horizon they consume a
fixed bsd budget, so everything after them - Kodi's own services, the probe listener,
add-on HTTP calls - can fail with ENOBUFS. Pruning startup services is therefore a
correctness fix here, not just a performance tweak. Suspects still enabled:
  plugin.googledrive, script.module.clouddrive.common  (both run a service)
  script.module.cocoscrapers (service entry, scraper module - probably no sockets)
Measured: after disabling The Crew alone the probe still could not bind on that run, so
there is at least one more consumer. A fresh boot binds the probe on attempt 1-2
(measured repeatedly), which is why file work is done in the first minute after a start.

WatchNixtoons2 keep-alive patch APPLIED ON THE DEVICE (18:1x):
  /switch/kodi/.kodi/addons/plugin.video.watchnixtoons2/lib/network.py
  adds  'Connection': 'close'  to the request headers, verified present in the file.
Not yet exercised - needs one real add-on run. If it fixes the ENOBUFS failure, the same
one-line treatment applies to other add-ons that keep a session alive across hosts.

Autostart of the PC media server (done, verified):
  Task Scheduler refused (non-elevated: "Access is denied" for both Register-ScheduledTask
  with S4U and schtasks /create ONLOGON), so it uses the per-user Startup folder:
  %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\Kodi Media Server.vbs
  -> launches python.exe (the binary the Windows Firewall allow-rule names) with
     mediaserver.py 8003 <user>\Videos, window hidden, logs to media-server.log
  Verified by starting it exactly that way: 200 on /, 206 Range, log lines written.

Second PC (no Python needed):
  C:\dev\kodi-switch\mediaserver.ps1  + start-mediaserver.bat
  Add-Type-compiled TcpListener server (HttpListener would need an admin urlacl).
  Tested against <user>\Videos on port 8013: directory listings (links now escape
  names but keep the trailing slash), 206 with Content-Range, HEAD, 403 on ../ traversal,
  404 for missing files, 1 MB streaming chunks, per-request MB/s logging.

## 2026-11-12 18:25 - playback guard refused HEVC because the hw flag was lost

First run of the filter-graph fix (b2c67c24) did not crash - it did not play either:
  boot.log:  NVTEGRA device rc=0
             playback guard: unsupported video, stopping

Application.cpp's SwitchPlaybackGuard refuses heavy codecs above 720p unless
DataCacheCore::IsVideoHwDecoder() is true. That flag comes from
CDVDVideoCodecFFmpeg::UpdateName() -> SetVideoDecoderName(m_name, m_pHardware != nullptr).
Our NVDEC decode runs inside a normal software AVCodecContext (m_pHardware stays null), so
the guard saw "software" and stopped 1080p HEVC one second in.

This also explains the earlier crash run: it got past the guard only because pix_fmt was
AV_PIX_FMT_NVTEGRA, which made Kodi treat the decoder as hardware by accident. Forcing
pix_fmt to NV12 (the filter fix) removed that accident.

Fix: an explicit m_nvtegraHw flag in CDVDVideoCodecFFmpeg, set when get_format() returns
AV_PIX_FMT_NVTEGRA (cleared on the software-retry path), and used by UpdateName() for both
the decoder name suffix ("ff-hevc-nvtegra") and the hardware argument. pix_fmt then stays
NV12 for the filter path while ProcessInfo still reports hardware.

Also relevant while testing: reading the probe's own add-on import can kill the probe
(Empty reply), and the live kodi.log cannot be read at all while Kodi runs - os.stat/open
return EIO because the FAT driver allows one writer and no second opener. Only kodi.old.log
(after a restart) and boot.log (our own traces) are readable mid-run.

## 2026-11-12 18:30 - The real crash: a failed thread creation, not the decoder

Atmosph�re finally produced a report for this one (all four of today's reports share the
same signature):
  Result 0x4A8 (2168-0002), Process hbloader, Type Data Abort, Address 0x20,
  Fault Address 0x0 - i.e. a null dereference, and the PC is in _write_r:
  the terminate handler faulted while writing its own trace. The interesting part is the
  stack that got there:

    CJobWorker::~CJobWorker
      CThread::~CThread
        std::__throw_system_error
          __cxa_throw -> __cxa_call_terminate -> terminate

  addr2line against build-kodi/kodi.bin resolves the frames around it to
  threadCreate() in libnx and hosversionIsAtmosphere(), i.e. the system_error comes from
  creating a thread, not from video code at all.

Reading it: libnx threadCreate fails when it cannot get a stack (memory pressure), libc++
reports that as std::system_error, and CThread::~CThread is implicitly noexcept - so one
failed thread creation during job-worker churn terminates the whole process. The video
path merely creates the pressure (3 MB P010 + 3 MB NV12 per frame at 1080p) and keeps the
job system busy; the decoder itself is fine, which matches "it played beautifully".

Three changes (overlay/xbmc/threads/Thread.cpp, overlay/xbmc/jobs/JobManager.cpp,
overlay/xbmc/application/Application.cpp):
  1. ~CThread: joinable() check + try/catch around detach(); a throw there can no longer
     kill the app.
  2. CThread::Create: catch the std::system_error, trace "CThread::Create FAILED <name>",
     leave m_thread null and return. IsRunning() is false, Join() returns false.
  3. CJobManager: only add a worker to m_workers if its thread really started - a worker
     with no thread would otherwise be counted as free forever and jobs would stop being
     handed out.
  4. The playback guard now logs mallinfo() every 5 s while video plays
     ("mem: alloc=<KB> free=<KB> playsec=<n>") so leak-vs-pressure can be read straight
     from boot.log instead of inferred.

Next: play the same file for minutes and read the mem: trace. If alloc climbs steadily it
is a leak; if it sits near a ceiling and thread creation fails, it is the 3 MB/frame churn
and the fix is to pool the transfer/conversion frames.

## 2026-11-12 18:50 - Why it dies: the NRO heap is only ~500 MB, and it is full

The mem: trace answered it in one run (no leak):
  mem: alloc=469962KB free=11401KB playsec=5
  mem: alloc=465076KB free=21407KB playsec=10
  mem: alloc=455280KB free=29171KB playsec=15
  mem: alloc=468254KB free=27421KB playsec=20
  mem: alloc=463535KB free=39052KB playsec=25
  mem: alloc=469455KB free=52052KB playsec=30
Flat allocation around 465 MB, free swinging between 11 and 52 MB - the process is
running against a ceiling, not leaking. At playsec=30 the run ended with

  CThread dtor: detach failed (ignored)

i.e. our new catch did fire, then the process disappeared with NO Atmosphere crash report
at all (newest report is still 15:30). A death without a fault report at a full heap is the
process being killed, not a fault in our code.

Where the 500 MB comes from: libnx's __libnx_initheap (nx/source/runtime/init.c) uses
envHasHeapOverride() first. hbloader provides a heap override for NROs, so libnx never runs
its "give me everything the process may use" branch:

    if (envHasHeapOverride()) { addr = envGetHeapOverrideAddr(); size = envGetHeapOverrideSize(); }
    else { svcGetInfo(TotalMemorySize/UsedMemorySize); size = total - used - 2 MB; svcSetHeapSize(&addr, size); }

Fix: __libnx_initheap is a WEAK symbol, so PlatformSwitch.cpp now defines it and computes
the heap from svcGetInfo(InfoType_TotalMemorySize / InfoType_UsedMemorySize,
CUR_PROCESS_HANDLE) minus an 8 MB reserve, aligned down to 2 MB. Fallbacks keep it safe:
if svcSetHeapSize refuses that size, use hbloader's override; if that fails too, 512 MB.
switch_preinit now traces "heap: <MB> (fake_heap a..b)" so the real size is visible in
boot.log on every start, and the memory trace also prints mallinfo's arena.

Expected after the next boot: heap: ~3000 MB, and free memory in the MB tens of thousands
during playback.

Earlier theory (threadCreate failing because of memory pressure) was a symptom of this, not
the cause: with 11 MB free, a 1 MB thread stack allocation fails, std::system_error is
thrown, and ~CThread terminates. Keeping the guards is still right - they turn a hard crash
into a recoverable event - but the heap is the actual fix.

## 2026-11-12 19:40 - SOLVED: HEVC over HTTP now plays continuously (1.007x)

Measured on build d9ad2e55, one file over http://<pc-ip>:8003 for 4m41s:
  sample 1: pos 130.8s 19:36:04
  sample 2: pos 161.0s 19:36:34
  sample 3: pos 191.2s 19:37:04
  sample 4: pos 221.2s 19:37:35
  sample 5: pos 251.4s 19:38:05
  sample 6: pos 281.4s 19:38:35
= 30.2 s of video per 30 s of wall clock, no stops, no crash, JSON-RPC answering
throughout. Previous failure point was ~97 s.

Two real fixes, and one wrong theory corrected:

1. ~CThread could throw (noexcept -> std::unexpected -> terminate).
   Crash report 19:05 (2168-0002) gives the exact chain:
     CJobWorker::~CJobWorker -> CThread::~CThread+0x74c -> std::unexpected
     -> __terminate -> (our handler) PC=_write_r
   The throw is inside StopThread()/CEvent::Wait(), NOT in detach() - an earlier
   guard around detach() alone did not help. ~CThread now runs its whole body
   under try/catch and writes "CThread dtor escaped: <what>" to boot.log. A
   self-deleting job worker can no longer take the process down.

2. The playback guard was stopping healthy hardware playback.
   "decoder starved" fired because NVDEC keeps the video queue at 0-1 while
   presenting at full rate. The rule now only applies when
   dataCache.IsVideoHwDecoder() is false.

CORRECTION to the 18:50 entry: the ~500 MB figure was mallinfo's arena (sbrk
high-water), not a limit. libnx's default __libnx_initheap reports 3084 MB on this
console (traced: "heap: 3084.1 MB (fake_heap 0x2e845f3000..0x2f45200000)"). The
heap override added at that time was therefore unnecessary AND harmful: calling
svcSetHeapSize a second time on an already-configured heap made Kodi exit
immediately after the window appeared ("regular Kodi exit", no crash report). It
is removed; the trace stays.

Open item: two runs died with a bare 2349-0004 (0x95D) kernel fatal and a 0-byte
crash report. That has not recurred since the destructor guard, so it may have
been the same escaping exception being handled at a different point.

Test-harness lessons (cost two wasted runs):
  - JSONRPC.Ping answers "pong", not "OK".
  - Player.Open can return OK while no player exists yet; playback needs ~20 s to
    appear with this file, so poll GetActivePlayers rather than assuming.

## 2026-11-12 19:55 - What the escaped destructor actually is, and who holds the sockets

Reading boot.log after the 10-minute playback (playsec 595, 1.007x throughout):
  escaped-dtor traces : 2   "CThread dtor escaped: Function not implemented"
  cleanup after throw : 2   "CThread dtor: cleanup after throw failed (leaking handle)"
  CThread::Create FAILED : 0
  stale thread           : 0
  mem: alloc ~480 MB, free ~143 MB, arena 624 MB, flat across 10 minutes

So the fault is ENOSYS ("Function not implemented") raised inside Kodi's thread
teardown - the std::future::wait_for() path in CThread::Join(), which needs a blocking
condition-variable wait. It happens about twice per 10 minutes of HEVC playback, and the
destructor guard converts it from a process-killing std::unexpected into a logged event
that leaks one thread handle. Acceptable for normal sessions; if multi-hour playback ever
misbehaves, the fix is to replace that wait_for() with a direct join (or a polling loop)
on TARGET_SWITCH.

Socket pool: after playback stopped the probe still could not bind. Disabling
plugin.googledrive and script.module.clouddrive.common made it bind on the FIRST attempt,
repeatedly failing before that. Those two services (not The Crew alone) are the main
long-lived socket holders at idle. They are disabled now; re-enable if cloud storage is
wanted. Keep this in mind before blaming an add-on for ENOBUFS: check what services are
running first.

## 2026-11-12 20:05 - WatchNixtoons2 WORKS (the biggest open item closes)

Playing a real episode from the site, resolved through the add-on's own handshake:
  Player item: https://undisk2.wcostream.com/getvid?evid=Kks11...  "Episode 1 Pilot"
  mem during playback: alloc ~311 MB, free ~293 MB (very comfortable)

Two things were needed, and neither alone was enough:

1. plugin.video.watchnixtoons2/lib/network.py now sends  'Connection': 'close'
   (applied on the device and verified in the file). The add-on keeps one requests
   session for the whole flow (homepage -> ad-verify -> embed -> getvid), and every host
   it touches held a keep-alive socket. On a fixed bsd budget that is what raised
   ENOBUFS: [Errno 105] No buffer space available, mid-flow.

2. plugin.googledrive and script.module.clouddrive.common are disabled. Measured: while
   they were enabled the probe could not bind 9998 at all (10 attempts); with them off it
   bound on the FIRST attempt, repeatedly. They were the idle socket holders, not The Crew.

Order of attack that works for any add-on ENOBUFS in future:
  a) list running services (Addons.GetAddons type=xbmc.service) and disable idle ones;
  b) patch the add-on to close connections per request;
  c) only then consider socket pool tuning (which broke other things every time).

This also settles the DASH question in principle: the demuxer was fine, it simply could
not get a socket. With the pool free an MPD/HLS playback should now get further.
Measured on that stream: 39.7s -> 60.1s position in 20.2s of wall clock = 1.01x realtime,
software H.264 path. The 1080p HEVC file from the local media server concurrently showed
1.007x on the hardware path, so both decode modes are now good.

## BACKLOG - Elementum / torrent streaming (idea for later, NOT started)

Goal: free sources without a debrid account, by streaming torrents directly (Elementum
style) instead of scraping hosters.

Why it is not a five-minute job on this platform:
  - Elementum is not a normal add-on: the Python part starts a separate native daemon
    ("elementum" binary) and talks to it over a local socket / its own sqlite store.
  - Horizon has no fork/exec: a homebrew process cannot spawn another executable from the
    SD card. So the daemon cannot exist as a separate binary here.
  - Therefore the only viable shape is: libtorrent compiled for aarch64-none-elf and linked
    INTO kodi.bin, with a small C++ bridge exposed to Python - i.e. the same pattern we used
    for inputstream.adaptive (static lib + dlopen shim + ADDON_Create entry points), but much
    bigger, because the torrent engine would have to live in-process and expose a Python API.
  - Dependencies to build for Switch: libtorrent (Rasterbar) + boost (or libtorrent 2.x with
    its own deps), OpenSSL (already in portlibs), zlib (already). Boost alone is a large port.
  - Memory/threads: libtorrent runs its own thread pool. Our bsd socket budget is separate
    from threads, but the process heap is ~3 GB and playback already uses ~450 MB. Feasible,
    but it has to be measured with the same mem: trace we now have.

Where reverse engineering (REA / slimREA) helps:
  - map the add-on <-> daemon protocol (methods, payloads, the sqlite schema) so a rebuilt
    in-process engine keeps compatibility with the existing Python front-end, or so the
    front-end can be rewritten against a clean C++ API;
  - recover the daemon's build configuration/flags and the exact libtorrent version used.

Realistic assessment: this is a multi-session porting project, not an add-on install. Free
scraper add-ons (CocoScrapers-backed) are the cheaper path to the same goal; revisit Elementum
only if scrapers prove unreliable.

## 2026-11-12 20:40 - Installing add-ons from the official repo, and why it looks broken

xbmc.executebuiltin('InstallAddon(<addonid>)') is asynchronous and silent:
  - it returns immediately, with no result and no on-screen progress;
  - the actual work (refresh the repo index, download the zip, unpack on FAT) takes
    30-90 seconds per add-on;
  - polling Addons.GetAddonDetails too early reports "Invalid params", which reads exactly
    like "install failed".
Measured: Pluto TV reported missing at +12 s and at +60 s, and was present a few minutes
later. The repo install does work - it is just slow and mute.

Rules for next time:
  - trigger with InstallAddon, then poll Addons.GetAddonDetails every 10 s for up to a minute
    before concluding anything;
  - give the user feedback with the Notification builtin, e.g.
    xbmc.executebuiltin('Notification(Switch, Installing <name>..., 4000)')
    so there is at least a toast while it happens;
  - official-repo installs resolve dependencies automatically (Pluto TV needed
    script.module.six/kodi-six/requests/simplecache/routing/inputstreamhelper - all present).

## 2026-11-12 21:20 - The Crew: keep the add-on, kill the startup health check

The Crew declares BOTH a plugin and a service:
  <extension library="thecrew.py" point="xbmc.python.pluginsource">
  <extension library="service.py"  point="xbmc.service" />      <- runs at every Kodi start
service.py does the "health check" and posts the toast, whether or not you open the add-on.
Disabling the add-on stops both, which also loses the add-on.

Fix used: remove ONLY that self-closing service element from
/switch/kodi/.kodi/addons/plugin.video.thecrew/addon.xml, keep the pluginsource and
xbmc.addon.metadata extensions, then UpdateLocalAddons. The plugin stays installed and
enabled; nothing runs it at startup any more. Takes effect on the next Kodi start.

TRAP (cost one repair): the service element is SELF-CLOSING. A non-greedy
  <extension[^>]*point="xbmc\.service"[^>]*>.*?</extension>
matches from it to the NEXT </extension>, which deleted the metadata extension as well -
the add-on then had no name, icon or summary. Repair: pull the original addon.xml out of the
add-on's own repo zip (thecrewwh/zips, path matrix/_zip/plugin.video.thecrew/...), strip the
self-closing tag with  <extension[^>]*point="xbmc\.service"[^>]*/> , and write it back.
Verify afterwards that both remaining extensions are listed and that Addons.GetAddonDetails
returns name/summary again.

## 2026-11-12 21:35 - "First playback works, everything after fails" = the socket pool, not the add-ons

User observation that cracked it: after a fresh boot the FIRST video plays, and every
playback after that fails - whichever add-on is used. That is a pool-exhaustion signature,
not a per-add-on bug. (The WatchNixtoons AttributeError from 20:57 was a separate,
intermittent site-parsing failure, and is now instrumented - see below.)

Why each playback costs so much pool:
  - libcurl keeps an idle connection cache per host. A closed stream's socket can stay
    parked there, still holding its bsd reservation.
  - CurlFile.cpp forced SO_RCVBUF = 256 KB on every curl socket, and the pool reserves
    rcvbuf * sb_efficiency(4) per socket - roughly 1 MB per socket from a small fixed pool.
So one playback could consume most of the pool's usable sockets, and the next found none.

Two changes, both in overlay/xbmc/filesystem/CurlFile.cpp:
  1. CURLOPT_FORBID_REUSE = ON (and CURLOPT_MAXCONNECTS = 1): close a socket when the
     transfer ends instead of parking it. This is the "release it when the add-on exits"
     behaviour that was asked for, applied to every curl use (browsing, artwork, playback).
  2. SO_RCVBUF 256 KB -> 128 KB: halves each socket's reservation (~2 MB/s / 16 Mbps, which
     is ample for streaming) at the cost of some peak throughput.

Measured with a 3x play -> stop -> play sequence over http://<pc-ip>:8003.

## 2026-11-12 22:05 - Pluto TV: add-on-level failure, network is fine

Symptom: choosing Pluto TV returns nothing and playback never starts ("fails instantly").

Evidence:
  - kodi.old.log: CGUIMediaWindow::GetDirectory(plugin://plugin.video.plutotv/ondemand) failed,
    GetDirectory - Error getting plugin://plugin.video.plutotv/, and CPythonInvoker reports
    "script successfully run" - i.e. the add-on handled the error itself and returned no items.
  - No m3u8/HLS lines at all: it never reached a stream, so this is not ISA, not playback,
    not the socket pool.
  - From the PC on the same network the endpoints answer fine:
      https://api.pluto.tv/v2/channels                                   -> 200, 1.1 MB
      https://service-channels.clusters.pluto.tv/v1/guide?start=..&stop=.. -> 200, 5.8 MB

So the add-on's own request/parse fails on-device while the API is healthy. Most likely a
timeout/truncation on the large (5.8 MB) guide response now that curl uses a 64 KB window,
or an API shape change, or Pluto rate limiting after heavy testing.

Next step if wanted: instrument plugin.video.plutotv's request helper the same way as
watchnixtoons (dump exception text + first part of the body to /switch/kodi/temp/), then
read the real error. Nothing else depends on it.

## BACKLOG - Live TV / PVR (status: nothing installed, not started)

Current state (measured 22:1x):
  Addons.GetAddons type=xbmc.pvrclient  -> none installed
  service.iptv.manager                  -> not registered
  settings pvrmanager.enabled           -> not present
So there is no PVR path at all right now; Live TV is not a settings problem, it is a
missing component.

Why it is missing: Kodi's Live TV needs a BINARY PVR client (pvr.iptvsimple, pvr.hts, ...)
and this port cannot dlopen binary add-ons - the same constraint we solved for
inputstream.adaptive by building it as a static library, linking it into kodi.bin and
serving its entry points from the switch_dlopen/switch_dlsym shim.

Path forward if wanted (option A): port pvr.iptvsimple the same way.
  - deps are comparatively kind: pugixml/tinyxml2, zlib, curl (no ffmpeg)
  - needs: static build for aarch64-none-elf, link into kodi.bin, ADDON_Create /
    GetTypeVersion / GetTypeMinVersion served from the shim, addon.xml with
    library_switch=<name>.a plus the zero-byte file trick
  - then M3U + XMLTV sources, EPG, groups and channel numbers work normally
Reality check: multi-hour job with its own debug loop, not a settings toggle.

Option B (no porting): plugin add-ons that list live streams (e.g. a TV add-on's Live
section) play fine through the normal player - no EPG, no recording. HLS playback itself
is not the blocker; ISA is linked in.

Also noted: this watchnixtoons2 fork (0.14.18) has NO download feature of its own -
grep for "download" over the whole add-on finds only a comment. Any "Download" seen in the
context menu is Kodi's generic action, not the add-on's.

## HOUSEKEEPING - card cleanup on next mount (not yet done)

Leftovers from diagnostics, safe to delete unless noted:

Root of the card:
  _write_test.bin            64 MB   write-speed test
  iotest.txt, iotest-raw.bin, iotest-none2.db, iotest-tpl.db, iotest-dot.db
  flag-a.db, flag-b.db, flag-empty.bin, empty-template.db
  probe-new.db, probe-tpl.db, probe-mem-new.db, probe-mem-tpl.db
  mo.bin, mhdt.txt            0 B    all zero-byte scratch files
  Metroid Dread.nsp        4,095.9 MB  KEEP or install - it sits exactly at the FAT32
                                       per-file limit, worth checking it is not truncated

/switch:
  nettest.nro                        our diagnostic tool
  DBI.nro.orig-912ru                  backup of the pre-DBIPatcher DBI (keep until sure)
  test-videos/                        only if it holds our test clips

Already cleaned on the PC side:
  <user>\Videos was full of zips/py/xml from testing (they appeared in Kodi as
  "PC Videos"). All moved to C:\dev\kodi-switch\http\tmp-scripts.

## 2026-11-13 - Exit is not slow: hbloader is

User tested it: launched from an **NSP forwarder** (an installed application title, full
memory via ApplicationOverride), Exit returns to the HOME menu immediately and cleanly.
From hbmenu the same build takes ~30 s.

So the 30 seconds was never Kodi's shutdown - it is hbloader tearing down the
applet-hosted NRO afterwards. Our own shutdown already returns early and skips
`Cleanup()`, which is why the image disappears long before the loader is done.

Consequence: the "slow exit" work is closed with no code change. Launch from a forwarder.
The README, ROADMAP and the store listing now say so.

## 2026-11-13 - The ENOSYS teardown leak: pthread_detach is a stub

Closed. The cause was not the condition-variable wait, and not `std::future` either.

**How it was found.** Instrumenting `~CThread` to print the thread name turned
"escaped: Function not implemented" into:

```
CThread dtor escaped [JobWorker]: Function not implemented
CThread dtor: cleanup after throw failed [JobWorker] (leaking handle)
```

Then reading the linked binary (`aarch64-none-elf-objdump` on `kodi.bin`) settled
it without guessing:

```
pthread_join:    bl __syscall_thread_join      <- real, implemented
pthread_detach:  mov w0, #0x58 ; ret           <- 88 = ENOSYS, does nothing
pthread_cond_timedwait:                        <- real (calls __libc_cond_wait)
```

So `std::thread::detach()` throws `std::system_error` on **every** call, while
`join()` works. `CJobWorker` is created with `Create(true)` ("kill ourselves when
we're done"), so when a job finishes the worker runs `~CThread` on its own thread.
`StopThread()` cannot join itself and leaves `m_thread` set, and the destructor then
called `detach()` - which threw out of an implicitly `noexcept` destructor. The
second trace is that same throw happening again in the cleanup path, which is why
the handle leaked.

**Fix (overlay `xbmc/threads/Thread.cpp`).**
- `Join()` polls an atomic completion flag instead of `std::future::wait_for()`.
- The destructor never calls `detach()` on Switch. When `m_thread` is still set
  after `StopThread()` - i.e. this is an auto-delete thread destroying itself - the
  `std::thread*` is parked in a small list. The next `CThread::Create()` on another
  thread `join()`s and deletes it, and `join()` is what reclaims the handle.
- Both destructor traces now include the thread name.
- Every boot writes one `kodi-switch build: threads <date> <time>` line, because
  `boot.log` is append-only across hundreds of launches and a measurement has to be
  attributable to a binary.

**Measured.** Same 7h file over HTTP, same machine:
- before: 1 escape by `playsec=65`, every session; 27 escapes over 222 boots total
- after: **0 escapes across `playsec=1120`** (18.7 min), `alloc` flat 457 -> 449 MB

**Also learned, and worth not re-learning:**
- Port **9090 is Kodi's raw JSON-RPC TCP socket**, not HTTP: request = 4-byte
  big-endian length + JSON, response = bare JSON with no prefix. Sending HTTP to it
  yields `HTTP/1.1 400 Bad Request` with no headers, which looks like a broken web
  server and is not. The HTTP UI is 8080 and is off.
- RPC answers **slowly during playback** (well over 30 s at times) while the console
  decodes; timeouts under a minute look like a dead channel and are not.

## 2026-11-14 - Screenshots, the accept-loop spin, and why the second playback fails

**Screenshots were silently broken.** Kodi has two `CScreenShot::TakeScreenshot`
overloads. Only `TakeScreenshot(filename, content)` was bypassed for Horizon; the
one the *screenshot action* calls, `TakeScreenshot(content)`, still went through
`CCaptureService`, and `LinuxRendererGLES` registers no capture provider, so
`Submit()` never completed and nothing was written - with no error anywhere.
Fixed by resolving the folder and calling the same `glReadPixels` path. Verified:
1280x720 PNGs appear and transfer off the console through the probe.
Also: with `debug.screenshotpath` unset the code opens a **folder dialog**, so a
"screenshot" can just put a modal on screen; and `xbmcvfs.exists()` returns false
for a directory that exists (use `os.makedirs`).

**JSON-RPC accept loop spun and leaked.** `CTCPServer::Run` sleeps only when
`accept()` fails with `EBADF`; libnx returns `ECONNABORTED` (113) when a pending
connection dies, and it can return it continuously. Measured 83,570 "duplicate
message" log lines in 10 s (~8,000 accept() calls/s) plus a leaked `CTCPClient`
per failure, because the object is allocated before `accept()` and never freed.
Now: the object is deleted, any failure backs off 100 ms, and logging is rate
limited.

**The second playback always fails, and it is not the network.** Measured with a
fresh process:
- playback 1: plays (`speed=1`, Fullscreen video)
- playback 2 and 3: `OK dialog` within ~0.1 s
- the media server log shows `206` for every attempt, so the data is being served
- Kodi's own trace: `DVDInputStreamFile::Open OK` then `... FAILED`
- free heap at that point is ~40 MB (`mem: alloc=435MB free=43MB arena=478MB`)
- socket gauge: 9 usable at startup, 4 later (probe itself holds some)
Earlier assumption that the pool is ~2 sockets is stale - it is 9 on this build.
Next step: log the curl/AVIO error at `DVDInputStreamFile::Open` failure and
sample free memory immediately before attempt 2; allocation failure inside the
player is the leading hypothesis.

**Also found:** `Addons33.db` reports `SQLITE_CORRUPT` on every start; the add-on
repository queries fail as a result. Needs a rebuild of that database.

## 2026-11-14 - Why "play, stop, play again" failed: a stale negative directory cache

The user's report: play a stream, stop it, and the next one fails unless you wait
about 30 seconds. Reproduced deterministically, then traced to a single line.

**Symptom.** Playback 1 plays. Playback 2 fails in ~0.1 s with "Playback failed".
The media server log shows `206` for every attempt, so the data was being served.

**How it was found.** Instrumenting every early return in `CFile::Open` gave:

```
CFile::Open enter  flags=0x71  url=.../supercut.mp4
CFile::Open dircache miss inCache=1
DVDInputStreamFile::Open FAILED
```

`inCache=1` means `CDirectoryCache::FileExists()` returned false *and* reported that
the path is in the cache - i.e. Kodi had cached "this file does not exist". The
next line, `if (bPathInCache) return false;`, then refuses the open **without any
network access at all**. When the entry expires, the same file plays again: that
is the ~30 seconds.

Why the entry exists: while the console is streaming, a directory listing can fail
(the media server is occupied), and Kodi stores that failure as "not there".

**Fix (`overlay/xbmc/filesystem/File.cpp`).** For internet streams, do not honour a
negative directory-cache entry; let the server answer. Verified on hardware:
5/5 consecutive playbacks with no waits, mixing a 7 GB file and a 10 s clip.

**Ruled out along the way** (all measured, none of them the cause):
- socket exhaustion: a startup gauge reports 9 usable sockets, not the 2 the old
  note assumed; the gauge also recovers after a stop
- memory: the failing open had *more* free heap than the successful one
  (`free=258950KB` vs `13710KB`)
- the file cache: `CFileCache::Open` was never reached - no `filecache:` trace fired
- codec/size: the same 7 GB file played fine on the first attempt
- the probe add-on: disabling it changed nothing

**Tried and reverted:** `SO_LINGER {1,0}` on curl sockets (to release an aborted
stream immediately). It did not fix the open, and it left Kodi stuck on the busy
dialog (window 10138) after every `Player.Stop`. Reverted.

**Still open:** stopping playback of a *large* HTTP stream leaves the busy dialog up
permanently (5 MB clip: clean; 7 GB file: stuck). The file-cache reader thread is
probably blocked in a socket read while `StopThread()` waits for it, but that is not
yet proven. Pre-existing or introduced by the `Join()` polling change is unknown -
the v22.0rc1 baseline is being tested by hand to decide.

**Also open:** `Addons33.db` reports `SQLITE_CORRUPT` on every start, so add-on
repository queries fail. Needs the database rebuilt.

## 2026-11-14 (later) - Hand test of v22.0rc1: it works. The "bugs" were my harness.

Rolled the console back to the released `v22.0rc1` NRO (sha256 8ab41dbd...) and had
the user drive it by hand rather than by script:

- dragonball supercut: play, exit, play again, seek forward an hour, back out,
  resume where it left off - no hiccups
- WatchNixtoons stream: stopped it, replayed **within 5 seconds** - no failure
- exit from the forwarder: immediate, as advertised

So the shipped build behaves correctly, and the failures reproduced earlier today
were produced by the test method, not by normal use. The harness stops playback with
a hard `Player.Stop` over JSON-RPC **mid-stream**; a person pressing stop/back closes
the stream cleanly. That difference is what left the busy dialog up and what put the
negative entry in the directory cache.

Lesson worth keeping: when a "bug" appears and disappears across builds that differ
only by trace lines, suspect the harness before the code. Two of today's three
"regressions" were that.

The harness is specifically hostile in two ways: it polls `Player.GetProperties`
every 2 seconds *during* playback, and it ends playback with a hard `Player.Stop`
mid-stream. On a console with ~9 sockets and a small heap, both are load a person
never applies. Any future playback test should let the file play without polling,
and stop it the way the UI does.

The `File.cpp` directory-cache change is still a defensible hardening (the trace
proved the negative entry exists and that Open refuses without any network access),
but it must be validated by hand, not by the script, before it ships.

Only real defect reported: one "One Pace" file that had to have its file type fixed
to play - unrelated to playback plumbing.

## 2026-11-14 - The guard was killing a file NVDEC plays: it judged before playback started

One Pace S18E01 (1080p 10-bit HEVC, 815 MB MKV over HTTP) was refused with
"HEVC at 1920x1080 decodes slower than real time". The file is fine.

**Evidence from boot.log**, same file, two builds:

```
before:  NVTEGRA device rc=0
         playback guard: unsupported video, stopping
         (no "NVTEGRA format selected" line at all)

after:   NVTEGRA device rc=0
         NVTEGRA format selected (hardware decode)
         NVTEGRA P010->NV12 scaler ready
         NVTEGRA: codec ctx advertised as NV12
```

So the hardware device was created successfully both times; the guard simply ran
while `avcodec_open2` was still in flight, saw `IsVideoHwDecoder() == false`, and
stopped the stream. That open takes **more than ten seconds** for this file, which
is why a first attempt with a 10-second grace period still failed intermittently -
`NVTEGRA format selected` appeared *after* the guard line in the log.

**Fix (`overlay/xbmc/application/Application.cpp`, SwitchPlaybackGuard).** Rule 1 now
requires `appPlayer->GetTime() > 5000` before refusing: a stream whose clock has not
advanced has not started playing and cannot be judged too slow. The starved-decoder
backstop (queue 0-1 for 8 consecutive seconds) is unchanged and still catches files
that genuinely cannot keep up.

**Verified on hardware.** The same MKV now plays with hardware decode; percentage
advanced 0.26% -> 6.71% over two minutes at speed 1, and the user confirmed video and
audio are correct.

Lesson: a "known impossible" heuristic must never fire before playback has actually
begun. Fast-opening files hid this for a long time because the flag was set before
the first one-second tick.

## 2026-11-14 - The clock was 6 hours ahead: our own mktime() substitution

Correction to the 2026-11-12 entry that blamed a missing TZ. TZ was fine: inside
Kodi, `time.ctime()` reported 14:46 local and `tzname` was ('CST','CDT'), while the
UI clock showed 20:46 - same minutes, hours offset by the UTC delta. So the libc was
right and Kodi's conversion was wrong.

Two port-local substitutions were responsible:

1. `xbmc/platform/posix/XTimeUtils.cpp`, `SystemTimeToFileTime()`: every other
   platform calls `timegm()` because the struct is defined as **UTC**. Switch called
   `mktime()`, which reads the same fields as *local* time, shifting every timestamp
   by the UTC offset. Fixed with an explicit civil-date -> epoch conversion (newlib
   has no timegm).
2. The same file's `GetTimezoneBias()` returned a hardcoded 0 on Switch (no
   `tm_gmtoff` in newlib), so `GetUTCDateTime()` could not compensate. Now derived by
   converting the broken-down local fields as if UTC and comparing with the real
   epoch.

Both are needed and they are consistent: local fields are stored as if UTC (Kodi's
model), and the bias converts back to true UTC where Kodi asks for it. Verified on
hardware: the UI clock now reads 14:57 local.

**Also fixed in the same pass, all visible in System info:**
- `KODI::MEMORY::GetMemoryStatus` was a stub returning zeros, so the screen showed
  `0MB / 0MB - 0%`. Now reports the process heap from `mallinfo()` plus libnx's
  `fake_heap_start`/`fake_heap_end`: measured `191MB / 3084MB - 6%`.
- Battery was the generic fallback's `return 0`. Now calls
  `psmGetBatteryChargePercentage`, but the screen still shows 0% - psm has not been
  confirmed to succeed from inside Kodi, so treat this as **unverified / still open**.
  (The clock, memory and OS name in that same screenshot are correct.)
- OS name was "Unknown OS" because neither `GetOsName()` nor `GetOsVersionInfo()` had
  a Switch branch. Now "Atmosphère on Horizon" when `/atmosphere` exists.

## 2026-11-14 - Screen dimmed mid-film: Horizon was never told media was playing

Symptom: while watching a video the panel dimmed to its low brightness, and touching
the screen or waking a controller brought it back. Most noticeable when a controller
powered off, because controller input was the only activity the console was counting.

Cause: the dimming is Horizon's own inactivity behaviour. A video application is
expected to call `appletSetMediaPlaybackState(true)` while it plays, which suppresses
the system's dim/auto-sleep timer; the port never sent it.

Fix (`overlay/xbmc/application/Application.cpp`, SwitchPlaybackGuard): set the flag when
video playback starts and clear it when playback stops or pauses, so normal dim/sleep
behaviour returns outside playback. Traced as `media playback state: on` / `off`.
Verified the flag is sent during playback; the dimming behaviour itself is being
confirmed by hand.

Not covered: audio-only playback still lets the console dim.

### Correction (same day): it was auto-sleep, not media state

`appletSetMediaPlaybackState(true)` alone did not stop the dimming - confirmed by hand.
Horizon's dim is the auto-sleep timer's warning stage, and the call that governs it is
`appletSetAutoSleepDisabled(true)`. Both are now set on playback start and cleared on
stop/pause, and both return `rc=0x0` on hardware:

```
media playback state: on (media rc=0x0, autosleep rc=0x0)
```

With auto-sleep disabled the panel now holds brightness for a whole film with the
controller asleep - confirmed by hand. Kodi's own screensaver was ruled out first:
`screensaver.mode` is empty, so nothing in Kodi was dimming.

## 2026-11-14 - Bluetooth audio verified

Hand-tested: playback over Bluetooth is clean, with no sync or stability problem
reported ("perfect"). The audio path is `CAESinkSwitch` -> `audout`, which Horizon
mixes, so Bluetooth is transparent to Kodi - no port-side latency compensation is
needed. Speakers, wired headphones and Bluetooth are now all confirmed on hardware.

## 2026-11-14 - Every repository listed nothing: corrupt Addons33.db, and a repair hook

Symptom: Settings -> Add-ons -> Install from repository showed nothing for any
repository, and the "Get more..." entry was missing from the skin selector. Kodi was
otherwise healthy - playback, JSON-RPC and add-on installs by id all worked.

Cause: `Addons33.db` was corrupt. `CAddonDatabase::GetRepositoryContent` failed with
`SQLITE_CORRUPT` on every start, so the add-on manager believed no repository offered
anything. Kodi hides "Get more..." when it cannot enumerate add-ons of that type, which
is why the option vanished rather than showing an error.

Why it could not be fixed in place: on FAT a file open for writing cannot be opened a
second time (`EIO`), and Kodi holds the only handle for the whole session. `os.rename`
from inside Kodi also fails with `EIO`.

Fix: a one-shot repair in `switch_preinit()`, before Kodi opens the database. Create
`/switch/kodi/repair-addons-db`; on the next start the port renames `Addons33.db` to
`Addons33.db.corrupt`, removes the marker and traces `addons db repair: rename rc=0`.
Kodi then creates a fresh database and re-registers the add-ons already on disk.

Cost, stated up front: a fresh database forgets enabled states and repo links. After
the repair only `repository.xbmc.org` was enabled; the other 64 repositories were
present but disabled, and the probe add-on needed re-enabling.

Result: the add-on manager reports **14 skins and 19 UI sound packs** available from
repositories, where it previously reported none. Confirmed by hand on the console:
"Install from repository" lists content again and the skin selector's "Get more..."
entry is back.

Note on installing a skin: a 7 MB skin download was enough to exhaust the BSD socket
pool and take JSON-RPC down for the duration (connections refused while the GUI stayed
responsive). It recovers, but it is the same pool limit that bit playback earlier -
installing several add-ons back to back will do it. Worth remembering before assuming
"remote control broke".

## 2026-11-14 - Official repository add-ons install and run (skins included)

With the add-ons database repaired, the normal install path works end to end:

- `skin.confluence` installed from the Kodi Add-on repository through the UI and was
  activated; Kodi rebuilt the texture cache and ran it (confirmed by hand)
- `resource.images.studios.white` installed earlier by id and is present
- the add-on manager reports 14 skins and 19 UI sound packs available from repos

So, installable and working today: **skins, UI sounds, resource images, and Python
scripts/services** - everything in the official repo that is not a compiled add-on.

Still out of reach: **binary add-ons** (PVR clients, visualisations, audio encoders,
screensavers with code). They cannot be `dlopen`ed on this platform; the working pattern
is to build them as static libraries and link them into the executable, as
`inputstream.adaptive` already is. That is the next meaningful step for repository
coverage.

Caveat measured along the way: a 7 MB skin download exhausts the BSD socket pool and
takes JSON-RPC down until it finishes (the GUI stays responsive). Installing several
add-ons in a row will hit the same limit.

## 2026-11-15 - Second static binary add-on: pvr.iptvsimple loads (still crashes later)

Goal: make an official-repository PVR client work, generalising the static-link
mechanism that inputstream.adaptive uses.

**Built from source.** Piers branch of kodi-pvr/pvr.iptvsimple (22.6.5, PVR API 9.2.0,
which matches our Kodi exactly). Dependencies built for aarch64-none-elf and installed
into the project portlibs: pugixml 1.14 and xz/liblzma 5.6.3; zlib came from devkitPro.

**Three obstacles, in the order they appeared:**

1. `ADDON_Create` / `ADDON_GetTypeVersion` / `ADDON_GetTypeMinVersion` are exported by
   every binary add-on, so two of them collide at link time. Fixed by compiling with
   `-DADDON_Create=PVR_ADDON_Create` (etc.) and dispatching in the shim on the add-on
   path (`switch_dlopen` returns a second handle for `pvr.iptvsimple`).
2. `kodi::addon::CPrivateBase::m_interface` is defined in each add-on by the
   `ADDONCREATOR` macro, so it collides too. `objcopy --redefine-sym` on the archive was
   tried and *did not work* - the add-on loaded but crashed in `kodi::Log` with
   `m_interface` null (`ldr x3, [x0, #24]`, x0 = 0), because the assignment and the read
   bound to different copies. `-Wl,--allow-multiple-definition` also failed, for the same
   reason: it hides the duplicate instead of separating it.
   **Working fix: compile the add-on with `-Dkodi=iptv_kodi`**, giving it its own copy of
   the entire SDK namespace. Verified in the archive:
   `_ZN9iptv_kodi5addon12CPrivateBase11m_interfaceE`.
3. Packaging: `library_switch="pvr.iptvsimple.a"` in the generated addon.xml plus a
   zero-byte file of that name, and the folder shipped in the runtime tree. The addon.xml
   also had to drop its `inputstream.ffmpegdirect` / `inputstream.rtmp` imports - both are
   binary add-ons we cannot load, and Kodi refuses to enable a client with missing deps.

**State at the end of the session:** Kodi starts, the client is enabled, and
`PVR_ADDON_Create returned 0` appears in boot.log - the load path works. But Kodi then
crashes once the PVR manager asks the client for channels (with a test M3U configured in
`addon_data/pvr.iptvsimple/settings.xml`). The newest crash report is on the SD card and
has not been read yet; that is the next step.

Also learned: `Settings.SetSettingValue` does not accept add-on settings in this Kodi
version (returns Invalid params), so add-on settings have to be written as
`addon_data/<addonid>/settings.xml`; and the add-on type enum is `kodi.pvrclient`, not
`xbmc.pvrclient`.

### pvr.iptvsimple: what is ruled out (measured, not inferred)

The client loads and `PVR_ADDON_Create returned 0`, but `Start()` never returns, so the
PVR manager sits at "PVR manager is starting up" and `PVR.GetChannels` answers
`-32100 Failed to execute method`.

From the source, `CIptvSimple::Start()` calls `m_playlistLoader.LoadPlayList()`
**synchronously** before it logs "Starting separate client update thread..." - and that
line never appears in kodi.log, which pins the stall inside `LoadPlayList()`.

Ruled out, each with evidence:

- **settings**: `addon_data/pvr.iptvsimple/settings.xml` on the card still holds
  `m3uPathType=1` and `m3uUrl=http://192.168.1.234:8003/test.m3u`
- **the server**: listening, `GET /test.m3u` -> 200, 295 bytes
- **our curl/VFS path**: `xbmcvfs.File(<same url>).read()` from the probe returns 295
  bytes in 0.0 s, so the exact path the add-on uses works
- **databases**: `PRAGMA integrity_check` = ok for TV48, Addons33, MyVideos149,
  Textures14 and Epg22 (the earlier corrupt Addons33 was rebuilt)
- **retry behaviour**: not the cause - the probe fetch is instant

Also observed: at 08:31 the console could not reach `mirrors.kodi.tv`
(`CCurlFile ... Could not connect to server`), while the LAN server answered. Internet
reachability on this console deserves its own check.

Leading hypothesis, not yet proven: PVR starts ~14 s into boot and the synchronous fetch
runs before WiFi association completes, so the connect blocks. Kodi does not log network
state at info level, so the next step is instrumentation - trace nifm/interface state at
preinit and around the first CFile::Open of an http:// URL - rather than more guessing.

Note for the next session: do NOT toggle the add-on with SetAddonEnabled while the PVR
manager is starting; that aborts the start and produced a false "could not validate M3U"
earlier.

## 2026-11-15 - Addons33.db rebuild side effect: every add-on comes back disabled

The one-shot `repair-addons-db` hook (committed 187797c) rebuilt a corrupt
`Addons33.db`. It restored repository listing and "Get more...", but it did **not**
carry over enabled states: after the rebuild 106 of 117 add-ons read `enabled: false`,
including `plugin.video.watchnixtoons2` - the one add-on that was actually in daily use.

Symptom the user reports: "it broke my add-on". The add-on is present, correct version,
files intact - only the flag is off, so it vanishes from the UI.

Diagnosis that separates this from a real breakage:

- `Addons.GetAddonDetails` -> `enabled: False` (this is the whole answer)
- `Files.GetDirectory` on a disabled plugin -> `-32602 Invalid params.`
- after `Addons.SetAddonEnabled {enabled: true}`, exercise a listing that really hits the
  network, not just the root menu (nixtoons builds its root menu locally):
  - root `plugin://plugin.video.watchnixtoons2/` -> 11 items, 4.6 s
  - `?action=actionCatalogMenu&path=latest` -> 16 items, 6.0 s, current 2026 episodes

So: **after any Addons33.db rebuild, re-enable the add-ons the user actually uses.**
Do not read "add-on missing from the list" as a code or packaging regression until the
enabled flag has been checked - the DB rebuild is the first suspect, not the last.

Recovery path for the enabled set: the pre-rebuild database is kept as
`Database/Addons33.db.corrupt`, and its `addon.enabled` column is the authoritative
record of what was on. Read it read-only (`file:...?mode=ro`, sqlite3 from the probe)
rather than asking the user to reconstruct the list by hand.

Also worth knowing when toggling: do not `SetAddonEnabled` while the PVR manager is
starting - it aborts the start and produces a false "could not validate M3U".

### Addons33.db rebuild: enabled set restored (closed)

Recovered the pre-rebuild enabled set from `Database.build-broken/Addons33.db`
(table `installed`, column `enabled`, opened with `mode=ro&immutable=1` - plain
`mode=ro` returns `disk I/O error` on FAT because SQLite tries to lock).
71 add-ons were enabled before the rebuild.

Re-enabled the 8 that were on and had come back off:

    script.module.beautifulsoup4   script.module.soupsieve
    script.module.kodi-six         script.module.simplejson
    script.module.pyqrcode         script.module.resolveurl
    script.module.inputstreamhelper  plugin.program.chef21

All verified `enabled=True`. `plugin.video.watchnixtoons2` was re-enabled first and
**the user confirmed on the console that it works** - menus open and content plays.
That is the acceptance test; RPC listings are not (they compete for the socket pool
and they are not usage).

Deliberately left off: `plugin.video.thecrew` (v3.2.2) plus its modules
`script.module.thecrew` and `script.thecrew.artwork`, and `script.module.cocoscrapers`.
The Crew declares `xbmc.service`, so it runs on every start and is a known socket-pool
consumer on this port. The user's call: leave it off unless it breaks something else.
Nothing else depends on it - `script.module.resolveurl` (the dependency other add-ons
actually use) is enabled.

Trap found while doing this, worth remembering: `Addons.GetAddons` **without** a type
filter does not report the `kodi.binary.*` / `xbmc.*` system add-ons, and the `enabled`
property in that bulk listing proved unreliable - a diff built on it reported
"0 were on and are now off" while `plugin.video.thecrew` was in fact still off.
`Addons.GetAddonDetails` with `properties: ["enabled","version"]` is the trustworthy
oracle. Do not add `type` to that properties list; it makes the whole call return
`-32602 Invalid params` for every add-on, which looks like mass breakage and is not.

Open, cosmetic: after the restore Kodi shows a "check the log for more information"
notification. The current session's `kodi.log` is locked (`EIO`), `boot.log` does not
exist on this build and `kodi.old.log` is the earlier 08:24 session, so it can only be
read after the next restart, when this session rotates into `kodi.old.log`.

## 2026-11-18 - Sockets are the real budget; two add-ons fixed (one by patch, one by service strip)

Machine rebuilt on the new PC (MINIMAJIN): WSL 3.0.5, Ubuntu imported from the
backup, devkitA64 16.1.0, full rebuild verified (969 targets, kodi.bin 130M,
kodi.nro 80M). Console is now 192.168.1.186, PC 192.168.1.223.

**Socket budget, measured inside Kodi via script.pyprobe /exec:**

    idle, EventServer + zeroconf + 4 services on : 2 free sockets (errno 105)
    after services.esenabled=False, zeroconf=False: 4 free (5 with 9090 down)
    after stripping 3 add-on services:            4 free (no change at idle)

So the stripped add-on services were not holding sockets at idle. What actually helped
was turning off EventServer and zeroconf: +2 sockets. `plugin.googledrive` and
`script.module.clouddrive.common` were already disabled from the 11-12 session.

**Playback does not leak a socket.** Measured free count before and after a
3-minute playback: 4 -> 4. The earlier "first plays, second fails" was NOT a leak;
that second title (The Little Match Girl) is simply a dead source. Betty Boop from
the same add-on plays fine, repeatedly.

**plugin.video.bnwmovie19 (Diggz) works** - first time. 720p-ish H.264 playing at
1x, screenshots confirm. This is the first non-official add-on to play on this port.
No code change; it needed the socket headroom.

**plugin.video.lookmovietomb - real bug, patched in place.** Error dialog, and
kodi.old.log gave the truth:

    UnboundLocalError: cannot access local variable 'play_item'
      main.py:635  play_item.setProperty('inputstream.adaptive.manifest_type','hls')

The add-on creates `play_item` only inside `if is_helper.check_inputstream():`, then
uses it unconditionally. inputstreamhelper cannot satisfy itself on this port, so the
branch never runs and the following line crashes. Fix (applied on the card, backup
`main.py.bak-inputstream`): build the ListItem before the branch, set the inputstream
properties only inside it. Result: Lunopolis (1:37:19) plays. _This is a genuine
add-on bug, not a port bug, and it is exactly the class a compatibility patcher
should encode: never assume the ISA helper succeeds._

**plugin.video.newson - not a code bug.** `script.module.simplecache` returns None:

    TypeError: 'NoneType' object does not support the context manager protocol
      simplecache.py _execute_sql -> with self._get_database()

`_get_database()` connects, runs SELECT, and on failure deletes and recreates the DB.
Both fail with "unable to open database file" while `Skin Helper Simplecache` logs
the same error on the same file the same second. FAT allows one handle per file, so
two consumers cannot share simplecache.db. The DB itself is fine (5.9 MB,
`pragma integrity_check` = ok, read-only). Untested fix: stop the skin-helper service
(`script.aeon.tajo.helper`) before using NewsON.

**Trap confirmed on this build:** 9090 is gated by `services.esenabled`. Turning
EventServer off to free a socket also kills JSON-RPC until it is turned back on -
re-enabling restored 9090 immediately, no restart needed. Do not disable it blind.

**Add-on service strip, applied on the card (backups `addon.xml.bak-service`):**
`plugin.video.lookmovietomb`, `plugin.video.newson`, `plugin.video.youtube`.
Measured services at boot: 4 -> 1 (only `script.aeon.tajo.helper`). Idle socket count
did not change, so the benefit is indirect (they stop holding sockets while active),
but it is harmless and keeps the pool free during browsing.

**Reading logs without file access:** `Files.PrepareDownload` is not implemented on
this build, and `/log?n=` cannot open kodi.log while Kodi holds it. What works:
restart Kodi, then read `/switch/kodi/temp/kodi.old.log` through
`script.pyprobe`'s `/exec` (it is closed by then). Tracebacks are multi-line; grep
for "EXCEPTION Thrown" and print the following ~25 lines.

## 2026-11-18 (later) - Live TV: playlist and channels work, PLAYBACK DOES NOT START

**Status, stated precisely:** the PVR manager starts, the client parses the M3U, and
channels appear (group "Test", then a real 5-channel live playlist). Pressing OK on a
channel - in the Guide or in the channel list - does nothing. `Player.Open` with
`channelid` returns OK but `Player.GetActivePlayers` stays empty, and `boot.log` shows
**no `CFile::Open` for the stream at all**, so the failure is before the player.

Ruled out:

- the stream: `xbmc.Player().play(<same m3u8>)` from the probe -> **PLAYING after 2s**,
  `getPlayingFile` returns the m3u8. Decoder and network are fine.
- ISA tagging: the client's `useInputstreamAdaptiveforHls` is already `false` and
  `defaultInputstream` is empty, so it is not handing the player an inputstream addon.
- EPG: playing from the channel list (no guide data) fails identically, so the empty
  EPG is not the cause. (It does make the Guide unplayable, separately.)

So the defect is in the PVR client -> player handover on this port: the client's
`GetChannelStreamProperties` result is not producing a playable item. That is inside
the statically linked pvr.iptvsimple (C++ in kodi.bin), not a Python add-on, so it
needs a build-level investigation, not a card patch.

**Root cause of the original stall (still valid and useful):** the client had no URL.
Kodi 19+ keeps per-instance client settings in
`addon_data/pvr.iptvsimple/instance-settings-1.xml`; `settings.xml` is legacy and
ignored. There the playlist was still at its default:

    <setting id="m3uPathType" default="true">1</setting>
    <setting id="m3uUrl" default="true" />          <- no value at all

`ConnectionManager::Process()` does `url.empty()` -> `SteppedSleep(interval)` ->
`continue`. No error is logged, `SetState(CONNECTED)` is never called, so
`CPVRManager::UpdateComponents()` keeps skipping the client and the manager sits at
STATE_STARTING. `LoadPlayList()` is never reached - which is why the 11-15 entry's
"pinned inside LoadPlayList" reading was wrong.

**Fix (applied, backups kept):** real values written into the instance file -

    <setting id="m3uPathType">1</setting>
    <setting id="m3uUrl">http://192.168.1.223:8003/test-live.m3u</setting>

`default="true"` is Kodi's marker for "user never set this"; the client treats it as
empty.

**Verified:** `PVR.GetProperties -> available: true`; 5 channels parsed from a real
live playlist (NASA TV, Red Bull TV, France 24, DW English, CBS News), all confirmed
alive from the PC minutes earlier.

**Secondary cause removed:** `sources.xml` ("PC Videos") and the PVR m3uUrl both still
pointed at `192.168.1.234`, the decommissioned laptop, so every UI load paid a
connect timeout against a dead host. Both now use `192.168.1.223` (this PC). The
media server serves `C:\dev\media` on 8003.

## 2026-11-18 - Tooling facts learned the hard way

- **9090 reply framing**: requests are 4-byte big-endian length-prefixed, but
  **replies are bare JSON with no prefix**. A client that waits for a length header
  hangs on every call. (`kodi-rpc.py` now tries both.)
- **`Files.PrepareDownload` is not implemented** on this build, so RPC cannot hand
  you a file. Log access is: restart Kodi, then read `kodi.old.log` (the live
  `kodi.log` returns `EIO` on FAT while Kodi holds it).
- **script.pyprobe reads only the first TCP segment** of a request (single
  `recv(8192)`), so `/exec` silently truncates payloads over ~1.4 KB. Upload big
  scripts in ~700-char chunks and exec the assembled file.
- **`/exec` shares one interpreter** across calls: a `resources` package imported by
  one add-on stays in `sys.modules` and poisons the next add-on's import. Purge
  `sys.modules` between runs. Same for dependency paths - Kodi resolves `<import>`
  transitively; a harness that only follows direct deps reports false
  `ModuleNotFoundError`s (`six`, `urllib3`).
- **Disabled is not broken.** 8 of 17 `plugin.video.*` add-ons were disabled after the
  Addons33.db rebuild; the UI looks identical. Check
  `Addons.GetAddonDetails -> enabled` first, every time.

## 2026-11-18 - Add-on failure taxonomy (measured, not guessed)

| Add-on | Class | Verdict |
|---|---|---|
| `bnwmovie19` | socket budget | works after +2 sockets |
| `lookmovietomb` | code bug (`play_item` unbound when ISA helper fails) | patched, plays |
| `popcornflix` | dead backend API | `api.unreel.me` returns 404, unfixable |
| `newson` | FAT single-handle contention on `simplecache.db` | unresolved |
| `pluto` | flagged `Broken` in Addons33.db; code runs clean in-process | unresolved |
| `crackle` | Widevine DRM | impossible on this port |

Socket pool measured in-process: 2 free with EventServer + zeroconf on, 4 with them
off (5 with 9090 down). Playback does not leak a socket (4 -> 4 after a 3-minute
play). Turning `services.esenabled` off **also kills 9090** on this build; turning it
back on restores it immediately, no restart.

## 2026-11-18 - Shippable backlog (constraint: anything shipped must work on a stock console)

The Homebrew App Store rule: a fix only counts if a fresh download reproduces it.
Everything done by hand on this console (service strips, instance-settings edits,
`.bak` files, disabled services) is diagnosis, not product.

**Ours to fix, in code:**

1. **Addons33.db repair must preserve enabled state.** The preinit hook renames a
   corrupt `Addons33.db` and Kodi recreates it empty; 106 of 117 add-ons came back
   `enabled: false`, and the user reads that as "it broke my add-ons". The old DB is
   kept as `Addons33.db.corrupt-<ts>`; the hook can read `installed.enabled` from it
   and re-apply the set before Kodi opens the database. Small, self-contained, ours.

2. **PVR channel playback never reaches the player.** Channels list, `Player.Open`
   with `channelid` returns OK, no player appears, and `boot.log` shows no
   `CFile::Open` for the stream. Same URL plays when handed to `xbmc.Player()`.
   Next step is a build: instrument `GetChannelStreamProperties` in the statically
   linked pvr.iptvsimple and confirm it is called and what it returns. Other
   homebrew PVR/IP电视 apps in the store prove the platform can do it.

3. **Socket budget.** 2 free sockets at idle is the real constraint behind several
   "add-on does nothing" reports. Belongs in `SocketInitConfig`, measured per
   variant from the preinit gauge, shipped as a build default - not as a user
   setting that changes 9090 behaviour.

**Not shippable, keep as notes only:** third-party add-on bugs (lookmovietomb
`play_item`), dead upstreams (popcornflix 404), DRM (crackle), per-card service
strips, and any hand-edited instance settings.

## 2026-11-18 - DB repair, first attempt: VACUUM is not usable on FAT

Tried the in-place recovery in `switch_preinit`: open `Addons33.db`, `VACUUM INTO` a
clean copy, swap it in. Built, deployed via netlink, ran on hardware. Result:

    addons db repair: recovery failed, rename rc=0, old db kept at
      /switch/kodi/.kodi/userdata/Database/Addons33.db.corrupt-1795057867

The fallback (the old behaviour) then ran, so Kodi recreated an empty DB and **all
add-ons came back disabled** - the exact regression the change was meant to stop.
Confirmed by `Addons.GetAddonDetails`: every entry `enabled=False`.

Why: **`VACUUM` cannot run on this filesystem.** Reproduced outside the port with
Python's sqlite3 on the same file - `VACUUM INTO` fails with `disk I/O error` even
when the source opens fine and `pragma integrity_check` is `ok` on the read-only
copy. So it is not a C++ bug and not a corrupt DB: SQLite's temp/journal handling
does not work on the SD's FAT via this devoptab. Any "repair the database in place"
design is dead.

**What still works:** the old DB is fully readable read-only
(`mode=ro&immutable=1`), and its `installed.enabled` column held 112 entries. The
enabled set was restored on the card through the add-on manager (84 applied; the 28
"failures" are `kodi.binary.*` system add-ons that cannot be toggled, which is
harmless). All dependency modules add-ons rely on are enabled
(`six`, `requests`, `routing`, `dateutil`, `t1mlib`, `urllib3`, `certifi`, `chardet`,
`idna`, `resolveurl`, `simplecache`, `kodi-six`, `beautifulsoup4`, `soupsieve`,
`simplejson`, `pyqrcode`, `inputstreamhelper`, `inputstream.adaptive`).

**Corrected design for the shippable fix:** no SQLite writes at all.

1. At preinit, read `installed.enabled` from the old DB **read-only** and write the
   list to a sidecar file (e.g. `addons-enabled.txt`).
2. After the add-on manager has loaded (post-DB-init), re-apply that list through
   `CAddonMgr` / `Addons.SetAddonEnabled` semantics.
3. Delete the sidecar when applied, so it is one-shot like the marker.

That is entirely within what this platform can do: read a file, write a text file,
call the add-on manager. It needs a hook after add-on load, which is the only open
design question.

**Trap worth remembering:** a Python `sqlite3.connect()` that is not closed on the
error path leaves a handle open, and on FAT every later `stat`/`open` of that file
returns `EIO` - it looks like the file vanished. Close in `finally`, always.
