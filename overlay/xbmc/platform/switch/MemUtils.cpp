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
#include <stdexcept>

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

  // Horizon's memory accounting is not exposed through newlib. Report unknowns
  // rather than reading a /proc file that does not exist.
  std::memset(buffer, 0, sizeof(*buffer));
}

} // namespace MEMORY
} // namespace KODI
