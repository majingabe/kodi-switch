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

#include <cstdio>

extern "C" void switch_trace(const char* msg);

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

  const u64 buttons = padGetButtonsDown(&m_pad);
  if (buttons == 0)
    return touchHandled;

  auto sendKey = [](XBMCKey sym) {
    XBMC_Event event{};
    event.type = XBMC_KEYDOWN;
    event.key.keysym.sym = sym;
    CServiceBroker::GetInputManager().OnEvent(event);
    event.type = XBMC_KEYUP;
    CServiceBroker::GetInputManager().OnEvent(event);
  };

  bool handled = true;

  if (buttons & HidNpadButton_A)
    sendKey(XBMCK_RETURN);
  else if (buttons & HidNpadButton_B)
    sendKey(XBMCK_ESCAPE);
  else if (buttons & HidNpadButton_X)
    sendKey(XBMCK_c);
  else if (buttons & HidNpadButton_Y)
    sendKey(XBMCK_i);
  else if (buttons & HidNpadButton_Up)
    sendKey(XBMCK_UP);
  else if (buttons & HidNpadButton_Down)
    sendKey(XBMCK_DOWN);
  else if (buttons & HidNpadButton_Left)
    sendKey(XBMCK_LEFT);
  else if (buttons & HidNpadButton_Right)
    sendKey(XBMCK_RIGHT);
  else if (buttons & HidNpadButton_L)
    sendKey(XBMCK_PAGEUP);
  else if (buttons & HidNpadButton_R)
    sendKey(XBMCK_PAGEDOWN);
  else if (buttons & HidNpadButton_ZL)
    sendKey(XBMCK_MINUS);
  else if (buttons & HidNpadButton_ZR)
    sendKey(XBMCK_PLUS);
  else if (buttons & HidNpadButton_Plus)
    sendKey(XBMCK_SPACE);
  else if (buttons & HidNpadButton_Minus)
    sendKey(XBMCK_BACKSPACE);
  else
    handled = false;

  if (handled)
    return true;

  return touchHandled;
}
