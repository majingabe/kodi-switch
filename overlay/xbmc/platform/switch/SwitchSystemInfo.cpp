/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "platform/posix/CPUInfoPosix.h"
#include "platform/posix/GPUInfoPosix.h"
#include "utils/CPUInfo.h"
#include "utils/GpuInfo.h"

#include <memory>

namespace
{
class CCPUInfoSwitch : public CCPUInfoPosix
{
public:
  CCPUInfoSwitch()
  {
    // Horizon OS exposes no /proc, so CCPUInfoPosix would report nothing.
    // Tegra X1: four Cortex-A57 cores (three available to applications).
    m_cpuVendor = "NVIDIA";
    m_cpuModel = "Tegra X1";
    m_cpuCount = 4;
    m_cpuFeatures = CPU_FEATURE_NEON;
    m_cores.resize(m_cpuCount);
    for (int i = 0; i < m_cpuCount; ++i)
      m_cores[i].m_id = i;
  }

  ~CCPUInfoSwitch() override = default;

  bool SupportsCPUUsage() const override { return false; }
  int GetUsedPercentage() override { return 0; }
  float GetCPUFrequency() override { return 1020.0f; } // docked clock, MHz
};

class CGPUInfoSwitch : public CGPUInfoPosix
{
public:
  ~CGPUInfoSwitch() override = default;

  bool GetGPUPlatformTemperature(CTemperature& temperature) const override { return false; }
};
} // namespace

std::shared_ptr<CCPUInfo> CCPUInfo::GetCPUInfo()
{
  return std::make_shared<CCPUInfoSwitch>();
}

std::unique_ptr<CGPUInfo> CGPUInfo::GetGPUInfo()
{
  return std::make_unique<CGPUInfoSwitch>();
}
