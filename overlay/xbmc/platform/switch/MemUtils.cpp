/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "utils/MemUtils.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <stdexcept>
#include <switch.h>

// Provided by libnx's newlib layer; the bounds of the process heap.
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

namespace KODI
{
namespace MEMORY
{

void* AlignedMalloc(size_t s, size_t alignTo)
{
  void* p = nullptr;
  int res = posix_memalign(&p, alignTo, s);
  if (res == EINVAL)
    throw std::runtime_error("Failed to align memory, alignment is not a multiple of 2");
  if (res == ENOMEM)
    throw std::runtime_error("Failed to align memory, insufficient memory available");
  return p;
}

void AlignedFree(void* p)
{
  if (p)
    free(p);
}

void GetMemoryStatus(MemoryStatus* buffer)
{
  if (!buffer)
    return;

  std::memset(buffer, 0, sizeof(*buffer));

  // Horizon does not expose /proc, but the process heap is measurable: libnx's
  // newlib layer keeps the heap bounds in fake_heap_start/fake_heap_end, and
  // mallinfo() reports how much of the heap is currently mapped (arena) and how
  // much of that is free (fordblks). Unmapped space is still available, so it
  // counts towards availPhys - otherwise the UI shows a false 100% load.
  const struct mallinfo mi = mallinfo();
  const uint64_t mapped = mi.arena > 0 ? static_cast<uint64_t>(mi.arena) : 0;
  const uint64_t freeMapped = mi.fordblks > 0 ? static_cast<uint64_t>(mi.fordblks) : 0;

  const uint64_t start = reinterpret_cast<uintptr_t>(fake_heap_start);
  const uint64_t end = reinterpret_cast<uintptr_t>(fake_heap_end);
  if (end <= start)
    return;

  const uint64_t heapSize = end - start;
  uint64_t available = freeMapped;
  if (mapped < heapSize)
    available += heapSize - mapped;
  if (available > heapSize)
    available = heapSize;

  buffer->totalPhys = heapSize;
  buffer->availPhys = available;
  buffer->memoryLoad = static_cast<unsigned int>(
      100.0 * (1.0 - static_cast<double>(available) / static_cast<double>(heapSize)));
}

} // namespace MEMORY
} // namespace KODI
