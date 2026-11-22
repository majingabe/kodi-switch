/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinEventsSwitch.h"

#include "ServiceBroker.h"
#include "input/InputManager.h"
#include "input/mouse/MouseStat.h"
#include "messaging/ApplicationMessenger.h"
#include "windowing/XBMC_events.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

extern "C" void switch_trace(const char* msg);

namespace
{
void SendKey(XBMCKey sym)
{
  XBMC_Event event{};
  event.type = XBMC_KEYDOWN;
  event.key.keysym.sym = sym;
  CServiceBroker::GetInputManager().OnEvent(event);
  event.type = XBMC_KEYUP;
  event.key.keysym.sym = sym;
  CServiceBroker::GetInputManager().OnEvent(event);
}
} // namespace

CWinEventsSwitch::CWinEventsSwitch()
{
  padInitializeDefault(&m_pad);
  hidInitializeTouchScreen();
  switch_trace("padInitializeDefault + touch init done");
}

bool CWinEventsSwitch::PumpTouch()
{
  HidTouchScreenState state{};
  if (hidGetTouchScreenStates(&state, 1) == 0 || state.count <= 0)
  {
    if (m_touchActive)
    {
      m_touchActive = false;
      XBMC_Event event{};
      event.type = XBMC_MOUSEBUTTONUP;
      event.button.button = XBMC_BUTTON_LEFT;
      event.button.x = m_touchX;
      event.button.y = m_touchY;
      CServiceBroker::GetInputManager().OnEvent(event);
      return true;
    }
    return false;
  }

  const HidTouchState& touch = state.touches[0];
  const uint16_t x = static_cast<uint16_t>(touch.x);
  const uint16_t y = static_cast<uint16_t>(touch.y);

  if (!m_touchActive || x != m_touchX || y != m_touchY)
  {
    m_touchX = x;
    m_touchY = y;

    XBMC_Event move{};
    move.type = XBMC_MOUSEMOTION;
    move.motion.x = x;
    move.motion.y = y;
    CServiceBroker::GetInputManager().OnEvent(move);

    if (!m_touchActive)
    {
      m_touchActive = true;
      XBMC_Event down{};
      down.type = XBMC_MOUSEBUTTONDOWN;
      down.button.button = XBMC_BUTTON_LEFT;
      down.button.x = x;
      down.button.y = y;
      CServiceBroker::GetInputManager().OnEvent(down);
    }
  }

  return true;
}

bool CWinEventsSwitch::PumpStick()
{
  if (!padIsConnected(&m_pad))
    return false;

  const HidAnalogStickState stick = padGetStickPos(&m_pad, 0);

  // ~1/3 of full deflection. Lower and a worn stick drifts; higher and diagonal
  // menu movement feels unresponsive.
  constexpr int deadzone = 11000;

  int dir = 0;
  if (std::abs(stick.x) > deadzone || std::abs(stick.y) > deadzone)
  {
    if (std::abs(stick.x) >= std::abs(stick.y))
      dir = stick.x > 0 ? 2 : 4; // right : left
    else
      dir = stick.y > 0 ? 1 : 3; // up : down
  }

  if (dir == 0)
  {
    m_stickDir = 0;
    return false;
  }

  const auto now = std::chrono::steady_clock::now();

  if (dir != m_stickDir)
  {
    // New direction: emit immediately, then repeat while held.
    m_stickDir = dir;
    m_stickFirst = true;
    m_stickNext = now;
  }

  if (now < m_stickNext)
    return false;

  switch (dir)
  {
    case 1:
      SendKey(XBMCK_UP);
      break;
    case 2:
      SendKey(XBMCK_RIGHT);
      break;
    case 3:
      SendKey(XBMCK_DOWN);
      break;
    default:
      SendKey(XBMCK_LEFT);
      break;
  }

  m_stickNext = now + (m_stickFirst ? std::chrono::milliseconds(400)
                                    : std::chrono::milliseconds(120));
  m_stickFirst = false;
  return true;
}

bool CWinEventsSwitch::MessagePump()
{
  const bool touchHandled = PumpTouch();

  padUpdate(&m_pad);

  static bool firstPump = true;
  if (firstPump)
  {
    firstPump = false;
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "pump connected=%d style=0x%x n1=0x%x n2=0x%x n3=0x%x hh=0x%x",
                  padIsConnected(&m_pad) ? 1 : 0, padGetStyleSet(&m_pad),
                  hidGetNpadStyleSet(HidNpadIdType_No1), hidGetNpadStyleSet(HidNpadIdType_No2),
                  hidGetNpadStyleSet(HidNpadIdType_No3), hidGetNpadStyleSet(HidNpadIdType_Handheld));
    switch_trace(buf);
  }

  bool handled = false;

  const u64 buttons = padGetButtonsDown(&m_pad);
  if (buttons & HidNpadButton_A)
    SendKey(XBMCK_RETURN);
  else if (buttons & HidNpadButton_B)
    SendKey(XBMCK_ESCAPE);
  else if (buttons & HidNpadButton_X)
    SendKey(XBMCK_c);
  else if (buttons & HidNpadButton_Y)
    SendKey(XBMCK_i);
  else if (buttons & HidNpadButton_Up)
    SendKey(XBMCK_UP);
  else if (buttons & HidNpadButton_Down)
    SendKey(XBMCK_DOWN);
  else if (buttons & HidNpadButton_Left)
    SendKey(XBMCK_LEFT);
  else if (buttons & HidNpadButton_Right)
    SendKey(XBMCK_RIGHT);
  else if (buttons & HidNpadButton_L)
    SendKey(XBMCK_PAGEUP);
  else if (buttons & HidNpadButton_R)
    SendKey(XBMCK_PAGEDOWN);
  else if (buttons & HidNpadButton_ZL)
    SendKey(XBMCK_MINUS);
  else if (buttons & HidNpadButton_ZR)
    SendKey(XBMCK_PLUS);
  else if (buttons & HidNpadButton_Plus)
    SendKey(XBMCK_SPACE);
  else if (buttons & HidNpadButton_Minus)
    SendKey(XBMCK_BACKSPACE);

  if (buttons != 0)
    handled = true;

  const bool stickHandled = PumpStick();

  return handled || stickHandled || touchHandled;
}
