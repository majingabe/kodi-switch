# scripts

Helper scripts used to build and deploy the port. They assume WSL2 with devkitPro
installed (`docs/BUILDING.md`).

| Script | What it does |
|---|---|
| `wsl-bootstrap.sh` | one-time host prep: devkitPro switch packages, portlibs dirs |
| `wsl-apply-overlay.sh` | copy `overlay/` into the Kodi tree and **touch** every file (see below) |
| `wsl-build-kodi.sh` | run the CMake/ninja build |
| `wsl-package-kodi.sh` | strip, NACP, icon→JPEG, `elf2nro`, stage the runtime tree |
| `wsl-ffmpeg-configure.sh` | configure the custom ffmpeg (adds https, png/mjpeg encoders, libxml2) |
| `wsl-ffmpeg-build.sh` | build that ffmpeg |
| `wsl-install-ffmpeg.sh` | install it into the portlibs prefix, keeping `.bak-orig` copies |
| `wsl-nxlink-run.sh` | push `kodi.nro` to a running netloader and launch it |
| `wsl-deploy-nro-verify.sh` | deploy, then verify the md5 on the card |
| `wsl-sync-one.sh` | copy a single overlay file and rebuild just that translation unit |

## Why `wsl-apply-overlay.sh` touches every file

`cp -a` preserves the source timestamps. If an overlay file is older than the object
file built from it, ninja considers it up to date and silently ships stale platform code.
That happened more than once. The script touches every copied file after copying, so the
overlay is always rebuilt.

## netloader is one-shot

`wsl-nxlink-run.sh` needs a `Y` press in hbmenu first. netloader accepts **one**
connection per press — testing the port with a TCP probe consumes that connection, so
"the port is open" checks right before a deploy can be the reason the deploy fails.
