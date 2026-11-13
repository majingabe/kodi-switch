/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "guilib/DispResource.h"
#include "threads/CriticalSection.h"
#include "utils/EGLUtils.h"
#include "windowing/WinSystem.h"

#include <memory>
#include <string>
#include <vector>

class CWinSystemSwitch : public CWinSystemBase
{
public:
  CWinSystemSwitch();
  ~CWinSystemSwitch() override = default;

  // CWinSystemBase
  bool InitWindowSystem() override;
  bool DestroyWindowSystem() override;
  bool CreateNewWindow(const std::string& name, bool fullScreen, RESOLUTION_INFO& res) override;
  bool DestroyWindow() override;
  bool ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop) override;
  bool SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays) override;
  void UpdateResolutions() override;
  bool Hide() override;
  bool Show(bool raise = true) override;
  bool MessagePump() override;
  std::unique_ptr<CVideoSync> GetVideoSync(CVideoReferenceClock* clock) override;

  const std::string GetName() override { return "switch"; }

  // EGL accessors used by the renderer and video path.
  EGLDisplay GetEGLDisplay() const;
  EGLSurface GetEGLSurface() const;
  EGLContext GetEGLContext() const;
  EGLConfig GetEGLConfig() const;

  void Register(IDispResource* resource) override;
  void Unregister(IDispResource* resource) override;

protected:
  bool CreateSurface();
  void DestroySurface();

  // Reconfigures the window when the console is docked or undocked while Kodi
  // is running (handheld 1280x720 <-> docked 1920x1080).
  void HandleDisplayModeChange();

  std::unique_ptr<KODI::WINDOWING::IOSScreenSaver> GetOSScreenSaverImpl() override;

  CEGLContextUtils m_eglContext;
  EGLNativeWindowType m_nativeWindow{nullptr};

private:
  CCriticalSection m_resourceSection;
  std::vector<IDispResource*> m_resources;
  bool m_docked{false};
};
