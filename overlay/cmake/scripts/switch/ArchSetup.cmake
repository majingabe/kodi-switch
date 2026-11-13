# Kodi platform setup for Horizon OS (Nintendo Switch homebrew).

set(ARCH_DEFINES -DTARGET_POSIX -DTARGET_SWITCH -D_GNU_SOURCE)
set(SYSTEM_DEFINES -D__STDC_CONSTANT_MACROS -D_FILE_OFFSET_BITS=64
                   -DHAS_POSIX_NETWORK)

set(CORE_MAIN_SOURCE ${CMAKE_SOURCE_DIR}/xbmc/platform/posix/main.cpp)

if(DEFINED ENV{KODI_SWITCH_PORTLIBS})
  include_directories("$ENV{KODI_SWITCH_PORTLIBS}/include")
  link_directories("$ENV{KODI_SWITCH_PORTLIBS}/lib")
endif()

# Kodi's internal dependency builds (exiv2, pcre2, spdlog, fstrcmp, ...) install
# their static libraries here.
link_directories("${CMAKE_BINARY_DIR}/build/lib")

set(PLATFORM_DIR platform/switch)
set(PLATFORMDEFS_DIR platform/posix)

if(WITH_ARCH)
  set(ARCH ${WITH_ARCH})
else()
  set(ARCH aarch64)
endif()
set(NEON True)
# NOTE: do not use -fPIC here. devkitA64 GCC 16.1.0 miscompiles TLS accesses when
# -fPIC (non-PIE) follows -fPIE: it emits "str xN, [x0, x0]" instead of adding the
# TLS offset, so every thread_local write lands at a bogus address and the first
# new thread aborts. -fPIE is already set in the toolchain; re-assert it last.
set(NEON_FLAGS "-mcpu=cortex-a57 -fPIE")

# Features that do not exist on Horizon OS or that pull in host-only libraries.
set(ENABLE_X11 OFF CACHE BOOL "" FORCE)
set(ENABLE_OPTICAL OFF CACHE BOOL "" FORCE)
set(ENABLE_CEC OFF CACHE BOOL "" FORCE)
set(ENABLE_VAAPI OFF CACHE BOOL "" FORCE)
set(ENABLE_VDPAU OFF CACHE BOOL "" FORCE)
set(ENABLE_ALSA OFF CACHE BOOL "" FORCE)
set(ENABLE_PULSEAUDIO OFF CACHE BOOL "" FORCE)
set(ENABLE_PIPEWIRE OFF CACHE BOOL "" FORCE)
set(ENABLE_SNDIO OFF CACHE BOOL "" FORCE)
set(ENABLE_DBUS OFF CACHE BOOL "" FORCE)
set(ENABLE_UDEV OFF CACHE BOOL "" FORCE)
set(ENABLE_LIRC OFF CACHE BOOL "" FORCE)
set(ENABLE_MDNS OFF CACHE BOOL "" FORCE)
set(ENABLE_AVAHI OFF CACHE BOOL "" FORCE)
set(ENABLE_AIRTUNES OFF CACHE BOOL "" FORCE)
set(ENABLE_UPNP OFF CACHE BOOL "" FORCE)
set(ENABLE_PYTHON ON CACHE BOOL "" FORCE)
# Cross-built CPython 3.14 for Horizon (see wsl-build-python.sh / wsl-make-python-prefix.sh).
set(Python3_EXECUTABLE "/usr/bin/python3" CACHE FILEPATH "" FORCE)
set(Python3_LIBRARY "/home/kidbu/kodi-switch/python-prefix/lib/libpython3.14.a" CACHE FILEPATH "" FORCE)
set(Python3_LIBRARIES "/home/kidbu/kodi-switch/python-prefix/lib/libpython3.14.a" CACHE FILEPATH "" FORCE)
set(Python3_INCLUDE_DIR "/home/kidbu/kodi-switch/python-prefix/include/python3.14" CACHE PATH "" FORCE)
set(Python3_INCLUDE_DIRS "/home/kidbu/kodi-switch/python-prefix/include/python3.14" CACHE PATH "" FORCE)
set(Python3_VERSION "3.14" CACHE STRING "" FORCE)
set(Python3_VERSION_MAJOR "3" CACHE STRING "" FORCE)
set(Python3_VERSION_MINOR "14" CACHE STRING "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_DVDCSS OFF CACHE BOOL "" FORCE)
set(ENABLE_BLURAY OFF CACHE BOOL "" FORCE)
set(ENABLE_LIBUSB OFF CACHE BOOL "" FORCE)
set(ENABLE_NFS OFF CACHE BOOL "" FORCE)
set(ENABLE_SMBCLIENT OFF CACHE BOOL "" FORCE)
set(ENABLE_MYSQLCLIENT OFF CACHE BOOL "" FORCE)
set(ENABLE_MARIADBCLIENT OFF CACHE BOOL "" FORCE)
set(ENABLE_EVENTCLIENTS OFF CACHE BOOL "" FORCE)
set(ENABLE_XSLT OFF CACHE BOOL "" FORCE)
set(ENABLE_LCMS2 OFF CACHE BOOL "" FORCE)
set(ENABLE_MICROHTTPD OFF CACHE BOOL "" FORCE)
# Kodi needs SWIG >= 4.5; use the host build in /usr/local (bundled one would be
# cross-compiled with our toolchain and fails on PCRE2).
set(ENABLE_INTERNAL_SWIG OFF CACHE BOOL "" FORCE)
set(SWIG_EXECUTABLE "/usr/local/bin/swig" CACHE FILEPATH "" FORCE)
# Kodi needs SWIG >= 4.5, which distros do not ship yet: build the bundled one.
# Use the portlibs build (static, no gnutls); Kodi's bundled copy wants HTTPS.
set(ENABLE_INTERNAL_LIBMICROHTTPD OFF CACHE BOOL "" FORCE)
set(ENABLE_UDFREAD OFF CACHE BOOL "" FORCE)
set(ENABLE_PLIST OFF CACHE BOOL "" FORCE)
set(ENABLE_CAP OFF CACHE BOOL "" FORCE)
set(ENABLE_BLUEZ OFF CACHE BOOL "" FORCE)
set(ENABLE_CDIO OFF CACHE BOOL "" FORCE)

set(ENABLE_INTERNAL_FFMPEG OFF CACHE BOOL "" FORCE)
set(ENABLE_INTERNAL_CURL OFF CACHE BOOL "" FORCE)
set(ENABLE_INTERNAL_FSTRCMP OFF CACHE BOOL "" FORCE)
set(ENABLE_INTERNAL_ASS OFF CACHE BOOL "" FORCE)

# libnx is the OS integration layer; link it after Kodi's own libraries.
list(APPEND DEPLIBS nx)

# Second pass over libpython: its statically linked extension modules sit after
# the objects that reference them, so a single archive scan misses them.
link_directories("/home/kidbu/kodi-switch/python-prefix/lib")
list(APPEND DEPLIBS python3.14)

# inputstream.adaptive is linked statically: Horizon has no dlopen(), so binary
# add-ons cannot be loaded at runtime. PlatformSwitch.cpp shims dlopen/dlsym to
# the add-on's three entry points. Bento4 and pugixml come from isa-deps.
link_directories("/home/kidbu/kodi-switch/build-isa")
link_directories("/home/kidbu/kodi-switch/build-isa/lib/mpegts")
link_directories("/home/kidbu/kodi-switch/build-isa/lib/webm_parser")
link_directories("/home/kidbu/kodi-switch/isa-deps/lib")
list(APPEND DEPLIBS inputstream.adaptive mpegts webm_parser ap4 pugixml)

# ffmpeg's DASH demuxer needs libxml2; static ffmpeg archives do not carry their
# dependencies. The archives are linked by full path after DEPLIBS, so libxml2 has
# to come last - CMake appends the standard libraries at the end of the line.
list(APPEND DEPLIBS xml2)
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -lxml2 -lz")
