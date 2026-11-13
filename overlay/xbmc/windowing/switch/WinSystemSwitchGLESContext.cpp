/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemSwitchGLESContext.h"

#include "utils/log.h"
#include "windowing/WindowSystemFactory.h"

void CWinSystemSwitchGLESContext::Register()
{
  KODI::WINDOWING::CWindowSystemFactory::RegisterWindowSystem(CreateWinSystem, "switch");
}

std::unique_ptr<CWinSystemBase> CWinSystemSwitchGLESContext::CreateWinSystem()
{
  return std::make_unique<CWinSystemSwitchGLESContext>();
}

bool CWinSystemSwitchGLESContext::ResizeWindow(int newWidth,
                                               int newHeight,
                                               int newLeft,
                                               int newTop)
{
  const bool result = CWinSystemSwitch::ResizeWindow(newWidth, newHeight, newLeft, newTop);
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight);
  return result;
}

bool CWinSystemSwitchGLESContext::SetFullScreen(bool fullScreen,
                                                RESOLUTION_INFO& res,
                                                bool blankOtherDisplays)
{
  if (!CWinSystemSwitch::SetFullScreen(fullScreen, res, blankOtherDisplays))
    return false;

  CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight);
  return true;
}

void CWinSystemSwitchGLESContext::SetVSyncImpl(bool enable)
{
  if (!m_eglContext.SetVSync(enable))
    CLog::Log(LOGERROR, "CWinSystemSwitchGLESContext: could not set egl vsync");
}

void CWinSystemSwitchGLESContext::PresentRenderImpl(bool rendered)
{
  if (!rendered)
    return;

  if (!m_eglContext.TrySwapBuffers())
    CEGLUtils::Log(LOGERROR, "CWinSystemSwitchGLESContext: eglSwapBuffers failed");
}
