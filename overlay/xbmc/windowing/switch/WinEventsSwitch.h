/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "windowing/WinEvents.h"

#include <chrono>

#include <switch.h>

class CWinEventsSwitch : public IWinEvents
{
public:
  CWinEventsSwitch();
  ~CWinEventsSwitch() override = default;

  bool MessagePump() override;

private:
  bool PumpTouch();
  bool PumpStick();

  PadState m_pad{};

  // Analog stick navigation. The stick is mapped onto the four directional
  // keys with a deadzone and key repeat: menus in Kodi only understand key
  // events, there is no joystick axis path on this window system.
  int m_stickDir{0};   // 0 none, 1 up, 2 right, 3 down, 4 left
  bool m_stickFirst{true};
  std::chrono::steady_clock::time_point m_stickNext{};

  // Touchscreen state. The panel is only present in handheld mode and always
  // reports coordinates in its native 1280x720 space, which matches the
  // handheld window 1:1.
  bool m_touchActive{false};
  uint16_t m_touchX{0};
  uint16_t m_touchY{0};
};
