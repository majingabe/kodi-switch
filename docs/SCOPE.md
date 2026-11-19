# Scope

What this port is for, so that effort goes to the right places.

## The goal

A Kodi media centre that works like Kodi on any other platform: open it, browse your
media, press play, and have it look and behave normally. It does not have to be a
feature-complete desktop build - it has to feel finished for the things a TV box
actually does.

## In scope (the main product)

- **Playback** of local and network media: H.264 and HEVC, software and NVDEC,
  seeking, subtitles, resume points, audio sync.
- **Library**: scanning, scraping, artwork, watched state, sorting - the things that
  make a media centre a library rather than a file browser.
- **Add-ons**: install, enable, run. The official repo and the common video add-ons
  are the benchmark, not an afterthought.
- **Skins and UI**: the default skin, plus the popular skins users install.
- **Audio**: speakers, headphones, Bluetooth.
- **Network sources**: HTTP(S) at minimum, since it is proven. SMB/NFS would be
  valuable if cheap; neither is a release blocker today.
- **Settings that behave**: changed settings survive a restart, and services start
  when they should.

## Out of scope (or explicitly deferred)

- **Optical media (CD/DVD).** There is no drive, no disc menu, no CD ripping. These
  code paths should be compiled out or hidden so they cannot confuse a user or waste
  testing time.
- **DRM (Widevine/PlayReady).** No CDM on this platform. Add-ons that require it
  cannot work and should not be presented as broken-by-us.
- **PVR / Live TV.** Nice to have, not the point. Channels list and the manager
  starts; playback handover is unsolved. Treat as a side quest that must not delay
  core work.
- **4K, HDR, colour management.** The renderer is 8-bit NV12; this needs a different
  GLES path and more bandwidth than stock clocks allow.
- **Gaming/emulation, retroplayer.** Not a goal for this port.

## How "done" is judged

A feature counts when a user who downloaded the build from the Homebrew App Store
gets the behaviour with no manual steps. Card edits, hand-patched add-on files and
per-console settings are diagnosis, never delivery.

## Consequences for the backlog

Priority order when choosing work:

1. Something that breaks ordinary use (playback, library scan, boot, exit).
2. Something that loses user data or state (add-on enabled flags, settings).
3. Something that makes many add-ons fail at once (socket budget).
4. One add-on or one feature (PVR, a single scraper).
