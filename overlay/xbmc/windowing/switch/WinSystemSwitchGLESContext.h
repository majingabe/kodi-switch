/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "WinSystemSwitch.h"
#include "rendering/gles/RenderSystemGLES.h"

class CWinSystemSwitchGLESContext : public CWinSystemSwitch, public CRenderSystemGLES
{
public:
  CWinSystemSwitchGLESContext() = default;
  ~CWinSystemSwitchGLESContext() override = default;

  static void Register();
  static std::unique_ptr<CWinSystemBase> CreateWinSystem();

  // CWinSystemBase via CWinSystemSwitch
  CRenderSystemBase* GetRenderSystem() override { return this; }

  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  bool SetFullScreen(bool fullScreen,
                     RESOLUTION_INFO& res,
                     bool blankOtherDisplays) override;

protected:
  void SetVSyncImpl(bool enable) override;
  void PresentRenderImpl(bool rendered) override;
};
