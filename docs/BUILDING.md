# Building

Target: `aarch64-none-elf`, built on Linux (WSL2 works) with devkitPro.
Host build was WSL2 Ubuntu; nothing in the build needs Windows.

## 1. Toolchain

Install devkitPro with the Switch tools:

```sh
# devkitPro pacman (see devkitpro.org for the keyring)
pacman -S switch-dev switch-pkg-config switch-tools
```

Every shell needs both the devkitPro profile **and** the Switch environment:

```sh
source /etc/profile.d/devkit-env.sh
source $DEVKITPRO/switchvars.sh      # exports CPPFLAGS, LDFLAGS, LIBS
```

`switchvars.sh` is not optional. Without it, `CPPFLAGS` lacks
`-isystem $DEVKITPRO/libnx/include`, so autotools/cmake checks for `netdb.h`,
`netinet/in.h` and friends fail and features disable themselves silently.

Two more traps worth knowing:

- autotools looks for `aarch64-none-elf-pkg-config`; if it is missing it falls back to
  `false` and every `check_pkg_config` fails with no message. Pass
  `--pkg-config=/usr/bin/pkg-config` and put the portlibs in `PKG_CONFIG_PATH`.
- **`<dlfcn.h>` is a trap**: devkitPro's header defines `dlopen`/`dlsym`/`dlclose` as
  `static inline` stubs. The port uses its own `switch_*` names for that reason.

## 2. Sources

```sh
git clone https://github.com/xbmc/xbmc.git kodi
cd kodi && git checkout 28ea2ea          # 22.0rc1 "Piers"
```

Optional binary add-on, built as a static library:

```sh
git clone https://github.com/xbmc/inputstream.adaptive.git
cd inputstream.adaptive && git checkout 22.3.24
```

## 3. ffmpeg for Switch (custom build)

devkitPro's `switch-ffmpeg` 7.1 package is the base, but its defaults are not enough
for a media centre:

- it ships **no image encoders**, so screenshots and thumbnails fail with
  *"You are missing a working encoder"* — add `--enable-encoder=png,mjpeg`;
- its protocol list omits **`https`** (TLS exists, but is not reachable) — add `https`;
- DASH needs libxml2 — add `--enable-libxml2`.

Build it with the same `target-os=horizon`, `--enable-libnx` (and `--enable-nvtegra` for
the hardware decoders), install into the portlibs prefix, then relink the app. The
scripts in `scripts/` reproduce this: `wsl-ffmpeg-configure.sh`, `wsl-ffmpeg-build.sh`,
`wsl-install-ffmpeg.sh`.

Keep the originals (`libav*.a.bak-orig`) so the port can be bisected later.

## 4. Configure and build Kodi

```sh
./scripts/wsl-apply-overlay.sh      # copy overlay/ over the kodi tree, then touch
./scripts/wsl-build-kodi.sh
```

`wsl-apply-overlay.sh` copies `overlay/` into the source tree **and touches every
copied file**. That touch matters: `cp -a` preserves timestamps, and an overlay file
older than its object file makes ninja skip the rebuild — which once shipped stale
platform code more than once.

Notes:

- the overlay also rewrites JSON-RPC schema files to LF; CRLF breaks the generated
  header, because the generator embeds input lines verbatim into C strings;
- `--pkg-config=/usr/bin/pkg-config` and the project-local portlibs prefix must be set
  (see `docs/ARCHITECTURE.md` for what depends on them).

## 5. Package

```sh
./scripts/wsl-package-kodi.sh
```

This strips the ELF (keeping `kodi.elf` for `addr2line`), builds a NACP with
`nacptool`, converts the icon to JPEG (the NRO icon must be JPEG), and runs `elf2nro`.
The runtime tree (`addons/`, `system/`, `python/`, `media/`) is staged next to it and
must be copied to `sdmc:/switch/kodi/`.

## 6. Deploy

```sh
./scripts/wsl-nxlink-run.sh <switch-ip>          # netloader: press Y in hbmenu first
./scripts/wsl-deploy-nro-verify.sh <switch-ip>   # deploy + verify md5
```

netloader accepts **one** connection per `Y` press. Probing the port with a TCP test
*consumes* it, which is why deployments sometimes fail right after a "port is open"
check.

## Licences of linked components

The NRO is a single binary, so their licences travel with it:

| Component | Licence |
|---|---|
| Kodi | GPL-2.0-or-later |
| inputstream.adaptive | GPL-2.0-or-later |
| ffmpeg | LGPL-2.1-or-later (GPL parts off unless enabled) |
| CPython | PSF Licence |
| libnx / devkitPro runtime | ISC / zlib-style |

If you ship the NRO, you must be able to provide the corresponding source, including
this overlay and your ffmpeg configuration.

## Testing notes

- Always launch from **full-memory hbmenu** (hold `R` on a title). Album applet mode
  cannot allocate enough for Kodi.
- `boot.log` on the SD is the port's own trace output (`sdmc:/switch/kodi/boot.log`);
  Kodi's `kodi.log` is locked while Kodi runs, so it only becomes readable after a
  restart, rotated to `kodi.old.log`.
- For crash reports, copy `/atmosphere/crash_reports/*.log` and symbolise with
  `aarch64-none-elf-addr2line -Cfipe build-kodi/kodi.bin <offset>`; the module base is
  reported by Atmosphère, and `kodi + 0x…` offsets map directly.
