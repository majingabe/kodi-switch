# Notices

This repository contains the Nintendo Switch platform layer for Kodi, plus patches to
Kodi itself. Kodi is licensed under **GPL-2.0-or-later** (see `LICENSE.md` and
`LICENSES/`); the port's own changes are released under the same terms.

Binary components linked into the resulting `.nro` keep their own licences:

| Component | Licence |
|---|---|
| Kodi | GPL-2.0-or-later |
| inputstream.adaptive | GPL-2.0-or-later |
| ffmpeg | LGPL-2.1-or-later (GPL components disabled unless explicitly enabled) |
| CPython | PSF Licence |
| libnx / devkitPro runtime | ISC / zlib-style |
| Bento4 (via inputstream.adaptive) | see upstream |

Building and distributing the resulting binary requires making the corresponding source
available, including this overlay and the ffmpeg configuration used.

Kodi is a trademark of the XBMC Foundation. This project is not affiliated with, or
endorsed by, the XBMC Foundation or Nintendo.
