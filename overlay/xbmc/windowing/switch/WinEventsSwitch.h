/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "windowing/WinEvents.h"

#include <switch.h>

class CWinEventsSwitch : public IWinEvents
{
public:
  CWinEventsSwitch();
  ~CWinEventsSwitch() override = default;

  bool MessagePump() override;

private:
  bool PumpTouch();

  PadState m_pad{};

  // Touchscreen state. The panel is only present in handheld mode and always
  // reports coordinates in its native 1280x720 space, which matches the
  // handheld window 1:1.
  bool m_touchActive{false};
  uint16_t m_touchX{0};
  uint16_t m_touchY{0};
};
