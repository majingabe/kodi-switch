if(CORE_SYSTEM_NAME STREQUAL switch)
  # Horizon has no mmap/memfd; Mmap/SharedMemory are only used by Wayland.
  set(SOURCES PosixInterfaceForCLog.cpp)
  set(HEADERS FileHandle.h
              PosixInterfaceForCLog.h)
else()
  set(SOURCES Mmap.cpp
              PosixInterfaceForCLog.cpp
              SharedMemory.cpp)
  set(HEADERS FileHandle.h
              Mmap.h
              PosixInterfaceForCLog.h
              SharedMemory.h)
endif()

core_add_library(platform_posix_utils)
