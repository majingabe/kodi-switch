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
#include "input/touch/generic/GenericTouchActionHandler.h"
#include "input/touch/generic/GenericTouchInputHandler.h"
#include "messaging/ApplicationMessenger.h"
#include "windowing/XBMC_events.h"

#include <algorithm>
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

  // Kodi's generic touch stack turns raw down/move/up into taps, long presses,
  // pans and multi-finger gestures. Without this the only way to use the panel
  // was to synthesise mouse events, which makes every skin draw a mouse pointer
  // and turns taps into an indirect cursor dance.
  CGenericTouchInputHandler::GetInstance().RegisterHandler(
      &CGenericTouchActionHandler::GetInstance());
  // Switch panel: 6.2" 1280x720 (~237 dpi) or 7" OLED (~210 dpi). 237 keeps the
  // swipe detector's distance threshold honest on both.
  CGenericTouchInputHandler::GetInstance().SetScreenDPI(237.0f);

  switch_trace("padInitializeDefault + touch init done");
}

CWinEventsSwitch::~CWinEventsSwitch()
{
  CGenericTouchInputHandler::GetInstance().UnregisterHandler();
}

bool CWinEventsSwitch::PumpTouch()
{
  HidTouchScreenState state{};
  const size_t states = hidGetTouchScreenStates(&state, 1);
  const int active = (states > 0) ? std::clamp(state.count, 0, 16) : 0;

  const int64_t time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();

  bool handled = false;
  bool seen[TouchSlots] = {};

  for (int i = 0; i < active; ++i)
  {
    const HidTouchState& touch = state.touches[i];
    const float x = static_cast<float>(touch.x);
    const float y = static_cast<float>(touch.y);

    // Keep a finger on the same Kodi pointer for its whole lifetime. Using the
    // array index directly would shift every other finger when one lifts.
    int slot = -1;
    for (int s = 0; s < TouchSlots; ++s)
    {
      if (m_touchSlots[s].active && m_touchSlots[s].fingerId == touch.finger_id)
      {
        slot = s;
        break;
      }
    }
    if (slot < 0)
    {
      for (int s = 0; s < TouchSlots; ++s)
      {
        if (!m_touchSlots[s].active)
        {
          slot = s;
          break;
        }
      }
    }
    if (slot < 0)
      continue; // Kodi's generic handler only supports two pointers.

    seen[slot] = true;
    if (!m_touchSlots[slot].active)
    {
      m_touchSlots[slot] = {touch.finger_id, true, x, y};
      CGenericTouchInputHandler::GetInstance().HandleTouchInput(TouchInputDown, x, y, time, slot);
      handled = true;
    }
    else if (x != m_touchSlots[slot].x || y != m_touchSlots[slot].y)
    {
      m_touchSlots[slot].x = x;
      m_touchSlots[slot].y = y;
      // This must come before HandleTouchInput. UpdateTouchPointer is what
      // marks the finger as moving once it passes the threshold; without it
      // the handler stays in "single touch" forever, so taps work but pans
      // (list scrolling) never start.
      CGenericTouchInputHandler::GetInstance().UpdateTouchPointer(slot, x, y, time);
      CGenericTouchInputHandler::GetInstance().HandleTouchInput(TouchInputMove, x, y, time, slot);
      handled = true;
    }
  }

  for (int s = 0; s < TouchSlots; ++s)
  {
    if (m_touchSlots[s].active && !seen[s])
    {
      CGenericTouchInputHandler::GetInstance().HandleTouchInput(
          TouchInputUp, m_touchSlots[s].x, m_touchSlots[s].y, time, s);
      m_touchSlots[s].active = false;
      handled = true;
    }
  }

  return handled;
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
