/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AESinkSwitch.h"

#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "utils/log.h"

#include <algorithm>
#include <cstring>

#include <switch.h>

namespace
{
constexpr unsigned int kSampleRate = 48000;

// audout needs buffers aligned to 0x1000.
constexpr size_t kAlign = 0x1000;
} // namespace

void CAESinkSwitch::Register()
{
  AE::AESinkRegEntry entry;
  entry.sinkName = "SWITCH";
  entry.createFunc = [](std::string& device, AEAudioFormat& desiredFormat)
      -> std::unique_ptr<IAESink> {
    auto sink = std::make_unique<CAESinkSwitch>();
    if (sink->Initialize(desiredFormat, device))
      return sink;
    return nullptr;
  };
  entry.enumerateFunc = CAESinkSwitch::EnumerateDevicesEx;
  entry.cleanupFunc = [] {};
  AE::CAESinkFactory::RegisterSink(entry);
}

void CAESinkSwitch::EnumerateDevicesEx(AEDeviceInfoList& list, bool /*force*/)
{
  CAEDeviceInfo info;
  info.m_deviceName = "default";
  info.m_displayName = "Switch Speaker";
  info.m_displayNameExtra = "";
  info.m_deviceType = AE_DEVTYPE_HDMI; // speaker and headphone both route here

  info.m_channels = AE_CH_LAYOUT_2_0;
  info.m_sampleRates.push_back(kSampleRate);
  info.m_dataFormats.push_back(AE_FMT_S16NE);
  info.m_dataFormats.push_back(AE_FMT_S16LE);
  info.m_dataFormats.push_back(AE_FMT_FLOAT);

  list.push_back(info);
}

bool CAESinkSwitch::Initialize(AEAudioFormat& format, std::string& /*device*/)
{
  Reclaim(true);

  // Force what audout can actually play; ActiveAE converts everything else.
  format.m_sampleRate = kSampleRate;
  format.m_dataFormat = AE_FMT_S16NE;
  format.m_channelLayout = AE_CH_LAYOUT_2_0;
  format.m_frameSize = format.m_channelLayout.Count() * (CAEUtil::DataFormatToBits(AE_FMT_S16NE) >> 3);
  format.m_frames = kBufferFrames;

  if (!OpenOutput(format))
    return false;

  m_format = format;
  m_inFlight = 0;
  m_nextPut = 0;

  const double bufferSeconds =
      static_cast<double>(kBufferFrames * kBufferCount) / static_cast<double>(kSampleRate);
  m_latency = static_cast<double>(kBufferFrames) / static_cast<double>(kSampleRate);

  CLog::Log(LOGINFO, "CAESinkSwitch: opened {} Hz, {} ch, {} buffers of {} frames ({} ms)",
            format.m_sampleRate, format.m_channelLayout.Count(), kBufferCount, kBufferFrames,
            bufferSeconds * 1000.0);

  return true;
}

bool CAESinkSwitch::OpenOutput(AEAudioFormat& format)
{
  const Result rcInit = audoutInitialize();
  if (R_FAILED(rcInit))
  {
    CLog::Log(LOGERROR, "CAESinkSwitch: audoutInitialize failed: 0x{:08x}", rcInit);
    return false;
  }

  u32 rate = kSampleRate;
  u32 channels = 2;
  PcmFormat pcm = PcmFormat_Int16;
  AudioOutState state{};
  char nameOut[128] = {};

  // audoutInitialize() already opened the default device at 48 kHz stereo S16.
  // libnx passes the channel count as (channels << 16); re-opening it here with
  // a plain "2" is rejected by the service (0xd401 = NotSupported), so use the
  // device libnx set up for us.
  rate = audoutGetSampleRate() ? audoutGetSampleRate() : kSampleRate;
  channels = audoutGetChannelCount() ? audoutGetChannelCount() : 2;
  pcm = audoutGetPcmFormat();
  CLog::Log(LOGINFO, "CAESinkSwitch: audout rate={} ch={} fmt={}", rate, channels,
            static_cast<int>(pcm));

  format.m_sampleRate = rate;
  format.m_channelLayout = AE_CH_LAYOUT_2_0;

  m_buffers.clear();
  m_buffers.resize(kBufferCount);
  const size_t bytes =
      static_cast<size_t>(kBufferFrames) * format.m_channelLayout.Count() * sizeof(int16_t);
  for (auto& b : m_buffers)
  {
    b.storage.assign(bytes + kAlign, 0);
    const uintptr_t raw = reinterpret_cast<uintptr_t>(b.storage.data());
    const size_t misalign = raw % kAlign;
    uint8_t* aligned = b.storage.data() + (misalign ? kAlign - misalign : 0);
    b.out = {};
    b.out.buffer = aligned;
    b.out.buffer_size = static_cast<u64>(bytes);
    b.out.data_size = 0;
    b.out.data_offset = 0;
    b.inFlight = false;
  }

  m_open = true;
  audoutStartAudioOut();
  return true;
}

void CAESinkSwitch::Deinitialize()
{
  if (!m_open)
    return;

  Reclaim(true);
  bool flushed = false;
  audoutFlushAudioOutBuffers(&flushed);
  audoutExit();

  m_open = false;
  m_buffers.clear();
  m_inFlight = 0;
}

void CAESinkSwitch::Reclaim(bool wait)
{
  if (!m_open || m_inFlight == 0)
    return;

  while (true)
  {
    AudioOutBuffer* released[8] = {};
    u32 count = 0;
    const Result rc = audoutWaitPlayFinish(released, &count, wait ? UINT64_MAX : 0);
    if (R_FAILED(rc))
      break;

    for (u32 i = 0; i < count; ++i)
    {
      for (auto& b : m_buffers)
      {
        if (&b.out == released[i] && b.inFlight)
        {
          b.inFlight = false;
          if (m_inFlight > 0)
            --m_inFlight;
          break;
        }
      }
    }

    if (!wait)
      break;
    if (m_inFlight == 0 || count == 0)
      break;
  }
}

unsigned int CAESinkSwitch::FreeBufferIndex() const
{
  for (size_t i = 0; i < m_buffers.size(); ++i)
  {
    const size_t idx = (m_nextPut + i) % m_buffers.size();
    if (!m_buffers[idx].inFlight)
      return static_cast<unsigned int>(idx);
  }
  return static_cast<unsigned int>(m_buffers.size());
}

unsigned int CAESinkSwitch::AddPackets(uint8_t** data, unsigned int frames,
                                       unsigned int offset)
{
  if (!m_open || !data || !data[0])
    return 0;

  std::lock_guard<std::mutex> lock(m_mutex);
  Reclaim(false);

  const unsigned int idx = FreeBufferIndex();
  if (idx >= m_buffers.size())
    return 0; // all buffers in flight; ActiveAE retries

  Buffer& b = m_buffers[idx];
  const unsigned int channels = m_format.m_channelLayout.Count();
  const unsigned int bytesPerFrame = channels * sizeof(int16_t);
  const unsigned int toCopy = std::min(frames, kBufferFrames);

  const uint8_t* src = data[0] + offset * bytesPerFrame;
  std::memcpy(b.out.buffer, src, static_cast<size_t>(toCopy) * bytesPerFrame);

  b.out.data_size = static_cast<u64>(toCopy) * bytesPerFrame;
  b.out.data_offset = 0;
  b.inFlight = true;
  ++m_inFlight;
  m_nextPut = (idx + 1) % static_cast<unsigned int>(m_buffers.size());

  const Result rc = audoutAppendAudioOutBuffer(&b.out);
  if (R_FAILED(rc))
  {
    b.inFlight = false;
    --m_inFlight;
    CLog::Log(LOGDEBUG, "CAESinkSwitch: append failed: 0x{:08x}", rc);
    return 0;
  }

  return toCopy;
}

void CAESinkSwitch::GetDelay(AEDelayStatus& status)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  Reclaim(false);

  const double queued = static_cast<double>(m_inFlight * kBufferFrames) /
                        static_cast<double>(m_format.m_sampleRate ? m_format.m_sampleRate
                                                                  : kSampleRate);
  status.SetDelay(queued);
}

double CAESinkSwitch::GetCacheTotal()
{
  return static_cast<double>(kBufferFrames * kBufferCount) /
         static_cast<double>(m_format.m_sampleRate ? m_format.m_sampleRate : kSampleRate);
}

void CAESinkSwitch::Drain()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  Reclaim(true);
}

CAESinkSwitch::~CAESinkSwitch()
{
  Deinitialize();
}
