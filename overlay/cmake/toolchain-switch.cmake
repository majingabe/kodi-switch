# Cross-compilation toolchain for Nintendo Switch homebrew (devkitA64 + libnx).
#
# Use from a devkitPro MSYS2 shell:
#   cmake -S <kodi> -B <build> -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=<kodi>/cmake/toolchain-switch.cmake \
#     -DCORE_SYSTEM_NAME=switch
#
# CORE_SYSTEM_NAME is kept separate from CMAKE_SYSTEM_NAME: CMake's Linux
# platform module gives us the POSIX/UNIX semantics Kodi expects, while
# CORE_SYSTEM_NAME=switch selects cmake/scripts/switch and cmake/platform/switch.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(_dkp_candidates "")
if(DEFINED ENV{DEVKITPRO})
  list(APPEND _dkp_candidates "$ENV{DEVKITPRO}")
endif()
list(APPEND _dkp_candidates "/opt/devkitpro" "C:/dev/msys64/opt/devkitpro")

set(DEVKITPRO "")
foreach(_candidate IN LISTS _dkp_candidates)
  if(EXISTS "${_candidate}/devkitA64/bin/aarch64-none-elf-gcc" OR
     EXISTS "${_candidate}/devkitA64/bin/aarch64-none-elf-gcc.exe")
    set(DEVKITPRO "${_candidate}")
    break()
  endif()
endforeach()

if(NOT DEVKITPRO)
  message(FATAL_ERROR "devkitPro not found. Set DEVKITPRO to the devkitPro root.")
endif()

if(EXISTS "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc.exe")
  set(_dkp_exe ".exe")
else()
  set(_dkp_exe "")
endif()

set(CMAKE_C_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc${_dkp_exe}")
set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-g++${_dkp_exe}")
set(CMAKE_ASM_COMPILER "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc${_dkp_exe}")
set(CMAKE_AR "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc-ar${_dkp_exe}")
set(CMAKE_RANLIB "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc-ranlib${_dkp_exe}")
set(CMAKE_NM "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc-nm${_dkp_exe}")
set(CMAKE_OBJCOPY "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-objcopy${_dkp_exe}")
set(CMAKE_OBJDUMP "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-objdump${_dkp_exe}")
set(CMAKE_STRIP "${DEVKITPRO}/devkitA64/bin/aarch64-none-elf-strip${_dkp_exe}")

set(CMAKE_FIND_ROOT_PATH
    "${DEVKITPRO}/portlibs/switch"
    "${DEVKITPRO}/libnx")

# Locally cross-built Switch libraries (libiconv, fontconfig, ...).
set(_kodi_local_include_flag "")
set(_kodi_local_library_flag "")
if(DEFINED ENV{KODI_SWITCH_PORTLIBS} AND EXISTS "$ENV{KODI_SWITCH_PORTLIBS}")
  list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{KODI_SWITCH_PORTLIBS}")
  set(_kodi_local_include_flag "-I$ENV{KODI_SWITCH_PORTLIBS}/include")
  set(_kodi_local_library_flag "-L$ENV{KODI_SWITCH_PORTLIBS}/lib")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT
    "${_kodi_local_include_flag} -isystem ${DEVKITPRO}/libnx/include -isystem ${DEVKITPRO}/portlibs/switch/include -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -D_GNU_SOURCE -DTIXML_USE_STL")
set(CMAKE_CXX_FLAGS_INIT
    "${_kodi_local_include_flag} -isystem ${DEVKITPRO}/libnx/include -isystem ${DEVKITPRO}/portlibs/switch/include -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -D_GNU_SOURCE -DTIXML_USE_STL")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${_kodi_local_library_flag} -L${DEVKITPRO}/libnx/lib -L${DEVKITPRO}/portlibs/switch/lib -specs=${DEVKITPRO}/libnx/switch.specs -Wl,-pie -Wl,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "${_kodi_local_library_flag} -L${DEVKITPRO}/libnx/lib -L${DEVKITPRO}/portlibs/switch/lib -specs=${DEVKITPRO}/libnx/switch.specs -Wl,-pie -Wl,--gc-sections")

# libnx first; libstdc++ is needed by Mesa's EGL static library. The trailing
# libraries satisfy static-only dependencies (Mesa -> libnx, Brotli, uuid).
set(CMAKE_C_STANDARD_LIBRARIES "-lswitchcompat -lnx -lm -lglapi -ldrm_nouveau -lnx -lm -lbrotlicommon -luuid")
set(CMAKE_CXX_STANDARD_LIBRARIES "-lswitchcompat -lnx -lstdc++ -lm -lglapi -ldrm_nouveau -lnx -lm -lbrotlicommon -luuid")

# Do not let CMake pull Linux-specific libraries that do not exist on Horizon.
set(CMAKE_DL_LIBS "")

# Make Kodi's FindPkgConfig see the Switch portlibs, not the host ones.
#
# devkitPro's aarch64-none-elf-pkg-config wrapper hard-resets PKG_CONFIG_LIBDIR
# to its own prefix, hiding our local cross-built libraries. Use the host
# pkg-config binary but restrict its search path explicitly.
find_program(KODI_PKG_CONFIG_EXECUTABLE NAMES pkg-config)
if(NOT KODI_PKG_CONFIG_EXECUTABLE)
  message(FATAL_ERROR "pkg-config not found on the host")
endif()
set(PKG_CONFIG_EXECUTABLE "${KODI_PKG_CONFIG_EXECUTABLE}")
set(PKG_CONFIG_ARGN "--static")

set(_dkp_pkgconfig "${DEVKITPRO}/portlibs/switch/lib/pkgconfig")
if(DEFINED ENV{KODI_SWITCH_PORTLIBS})
  # Local cross-built libraries win over devkitPro's older copies (notably curl).
  set(_dkp_pkgconfig "$ENV{KODI_SWITCH_PORTLIBS}/lib/pkgconfig:${_dkp_pkgconfig}")
endif()
set(ENV{PKG_CONFIG_LIBDIR} "${_dkp_pkgconfig}")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")

# Kodi's add-on ABI uses CPU/ARCH; keep them explicit for the target.
set(CPU aarch64 CACHE STRING "")
set(WITH_CPU cortex-a57 CACHE STRING "")
