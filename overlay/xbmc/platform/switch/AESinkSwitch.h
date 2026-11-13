/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/AudioEngine/Interfaces/AESink.h"
#include "cores/AudioEngine/Utils/AEDeviceInfo.h"

#include <atomic>
#include <mutex>
#include <vector>

#include <switch.h>

// Audio sink for Horizon OS built on libnx's audout service.
// audout gives us a fixed 48 kHz, 16-bit output, so the sink reports that
// format and lets Kodi's engine resample/convert into it.
class CAESinkSwitch : public IAESink
{
public:
  CAESinkSwitch() = default;
  ~CAESinkSwitch() override;

  static void Register();
  static void EnumerateDevicesEx(AEDeviceInfoList& list, bool force);

  const char* GetName() override { return "SWITCH"; }

  bool Initialize(AEAudioFormat& format, std::string& device) override;
  void Deinitialize() override;

  double GetCacheTotal() override;
  double GetLatency() override { return m_latency; }

  unsigned int AddPackets(uint8_t** data, unsigned int frames, unsigned int offset) override;
  void GetDelay(AEDelayStatus& status) override;
  void Drain() override;

private:
  struct Buffer
  {
    AudioOutBuffer out{};
    std::vector<uint8_t> storage;
    bool inFlight{false};
  };

  bool OpenOutput(AEAudioFormat& format);
  void Reclaim(bool wait);
  void Append();
  unsigned int FreeBufferIndex() const;

  static constexpr unsigned int kBufferFrames = 1024;
  static constexpr size_t kBufferCount = 5;

  AEAudioFormat m_format{};
  std::vector<Buffer> m_buffers;
  unsigned int m_nextPut{0};
  unsigned int m_inFlight{0};
  std::mutex m_mutex;
  bool m_open{false};

  double m_latency{0.0};
};
