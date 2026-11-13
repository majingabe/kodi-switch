/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemSwitch.h"

#include "WinEventsSwitch.h"
#include "ServiceBroker.h"
#include "cores/VideoPlayer/VideoRenderers/LinuxRendererGLES.h"
#include "settings/DisplaySettings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/OSScreenSaver.h"
#include "windowing/Resolution.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>

#include <switch.h>

extern "C" void switch_trace(const char* msg);

CWinSystemSwitch::CWinSystemSwitch()
{
  switch_trace("CWinSystemSwitch ctor");
  m_nativeWindow = reinterpret_cast<EGLNativeWindowType>(nwindowGetDefault());
  m_winEvents = std::make_unique<CWinEventsSwitch>();
  m_docked = appletGetOperationMode() == AppletOperationMode_Console;
}

bool CWinSystemSwitch::InitWindowSystem()
{
  switch_trace("InitWindowSystem enter");
  if (!CWinSystemBase::InitWindowSystem())
    return false;

  // Normally the platform window system registers the video renderers (X11,
  // Gbm, Android and Wayland all do this). Without it CRendererFactory stays
  // empty, CRenderManager gets a null renderer and no video frame can ever be
  // configured.
  CLinuxRendererGLES::Register();
  switch_trace("video renderer registered");

  switch_trace("eglGetDisplay");
  if (!m_eglContext.CreateDisplay(EGL_DEFAULT_DISPLAY))
  {
    CLog::Log(LOGERROR, "CWinSystemSwitch: eglGetDisplay failed");
    switch_trace("eglGetDisplay FAILED");
    return false;
  }

  switch_trace("eglInitialize");
  if (!m_eglContext.InitializeDisplay(EGL_OPENGL_ES_API))
  {
    CLog::Log(LOGERROR, "CWinSystemSwitch: eglInitialize failed");
    switch_trace("eglInitialize FAILED");
    return false;
  }

  switch_trace("eglChooseConfig");
  if (!m_eglContext.ChooseConfig(EGL_OPENGL_ES2_BIT))
  {
    CLog::Log(LOGERROR, "CWinSystemSwitch: no matching EGL config");
    switch_trace("eglChooseConfig FAILED");
    return false;
  }

  switch_trace("eglCreateContext");
  CEGLAttributesVec contextAttribs;
  contextAttribs.Add({{EGL_CONTEXT_CLIENT_VERSION, 2}});
  if (!m_eglContext.CreateContext(contextAttribs))
  {
    CLog::Log(LOGERROR, "CWinSystemSwitch: eglCreateContext failed");
    switch_trace("eglCreateContext FAILED");
    return false;
  }

  CLog::Log(LOGINFO, "CWinSystemSwitch: EGL context created");
  switch_trace("InitWindowSystem done");
  return true;
}

bool CWinSystemSwitch::DestroyWindowSystem()
{
  DestroyWindow();
  m_eglContext.Destroy();
  CWinSystemBase::DestroyWindowSystem();
  return true;
}

bool CWinSystemSwitch::CreateNewWindow(const std::string& name,
                                       bool fullScreen,
                                       RESOLUTION_INFO& res)
{
  switch_trace("CreateNewWindow enter");
  DestroySurface();

  m_nWidth = res.iWidth;
  m_nHeight = res.iHeight;
  m_fRefreshRate = res.fRefreshRate;
  m_bFullScreen = fullScreen;

  const int width = res.iScreenWidth > 0 ? res.iScreenWidth : res.iWidth;
  const int height = res.iScreenHeight > 0 ? res.iScreenHeight : res.iHeight;

  Result rc = nwindowSetDimensions(nwindowGetDefault(), width, height);
  if (R_FAILED(rc))
  {
    CLog::Log(LOGWARNING, "CWinSystemSwitch: nwindowSetDimensions({}x{}) failed: 0x{:08x}",
              width, height, rc);
    switch_trace("nwindowSetDimensions failed");
  }

  switch_trace("eglCreateWindowSurface");
  if (!CreateSurface())
  {
    switch_trace("CreateSurface FAILED");
    return false;
  }

  switch_trace("eglMakeCurrent");
  if (!m_eglContext.BindContext())
  {
    CLog::Log(LOGERROR, "CWinSystemSwitch: eglMakeCurrent failed");
    switch_trace("BindContext FAILED");
    return false;
  }

  m_bWindowCreated = true;
  CLog::Log(LOGINFO, "CWinSystemSwitch: window {}x{} @ {:.2f} Hz", m_nWidth, m_nHeight,
            m_fRefreshRate);
  switch_trace("CreateNewWindow done");
  return true;
}

bool CWinSystemSwitch::DestroyWindow()
{
  DestroySurface();
  m_bWindowCreated = false;
  return true;
}

bool CWinSystemSwitch::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  m_nWidth = newWidth;
  m_nHeight = newHeight;
  return true;
}

bool CWinSystemSwitch::SetFullScreen(bool fullScreen,
                                     RESOLUTION_INFO& res,
                                     bool blankOtherDisplays)
{
  return CreateNewWindow("", fullScreen, res);
}

void CWinSystemSwitch::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
  const int width = docked ? 1920 : 1280;
  const int height = docked ? 1080 : 720;

  RESOLUTION_INFO res;
  UpdateDesktopResolution(res, "Switch", width, height, 60.0f, 0);

  CServiceBroker::GetWinSystem()->GetGfxContext().ResetOverscan(res);

  CDisplaySettings::GetInstance().ClearCustomResolutions();
  CDisplaySettings::GetInstance().AddResolutionInfo(res);
  CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = res;
  CDisplaySettings::GetInstance().ApplyCalibrations();

  CLog::Log(LOGINFO, "CWinSystemSwitch: mode {}x{} ({})", width, height,
            docked ? "docked" : "handheld");
}

bool CWinSystemSwitch::Hide()
{
  return true;
}

bool CWinSystemSwitch::Show(bool raise)
{
  return true;
}

bool CWinSystemSwitch::MessagePump()
{
  HandleDisplayModeChange();
  return m_winEvents && m_winEvents->MessagePump();
}

void CWinSystemSwitch::HandleDisplayModeChange()
{
  // Polling the applet service every frame is an IPC call per frame. With the
  // frame loop already slow that hammers the applet/BSD services and competes
  // with network threads, so check once a second instead.
  static auto lastCheck = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  if (now - lastCheck < std::chrono::seconds(1))
    return;
  lastCheck = now;

  const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
  if (docked == m_docked)
    return;

  m_docked = docked;

  const int width = docked ? 1920 : 1280;
  const int height = docked ? 1080 : 720;

  char buf[128];
  std::snprintf(buf, sizeof(buf), "display mode -> %s (%dx%d)",
                docked ? "docked" : "handheld", width, height);
  switch_trace(buf);

  RESOLUTION_INFO res;
  UpdateDesktopResolution(res, "Switch", width, height, 60.0f, 0);
  CServiceBroker::GetWinSystem()->GetGfxContext().ResetOverscan(res);

  CDisplaySettings::GetInstance().ClearCustomResolutions();
  CDisplaySettings::GetInstance().AddResolutionInfo(res);
  CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP) = res;
  CDisplaySettings::GetInstance().ApplyCalibrations();

  // Recreates the EGL surface through CreateNewWindow() and makes Kodi rebuild
  // its GUI at the new resolution.
  CServiceBroker::GetWinSystem()->GetGfxContext().SetVideoResolution(RES_DESKTOP, true);

  {
    std::lock_guard<CCriticalSection> lock(m_resourceSection);
    for (auto* resource : m_resources)
      resource->OnResetDisplay();
  }

  switch_trace("display mode applied");
}

std::unique_ptr<CVideoSync> CWinSystemSwitch::GetVideoSync(CVideoReferenceClock* clock)
{
  // The on-screen UI does not need a video reference clock yet. Video playback
  // will provide a Switch implementation in the playback milestone.
  return nullptr;
}

bool CWinSystemSwitch::CreateSurface()
{
  return m_eglContext.CreateSurface(m_nativeWindow);
}

void CWinSystemSwitch::DestroySurface()
{
  m_eglContext.DestroySurface();
}

EGLDisplay CWinSystemSwitch::GetEGLDisplay() const
{
  return m_eglContext.GetEGLDisplay();
}

EGLSurface CWinSystemSwitch::GetEGLSurface() const
{
  return m_eglContext.GetEGLSurface();
}

EGLContext CWinSystemSwitch::GetEGLContext() const
{
  return m_eglContext.GetEGLContext();
}

EGLConfig CWinSystemSwitch::GetEGLConfig() const
{
  return m_eglContext.GetEGLConfig();
}

void CWinSystemSwitch::Register(IDispResource* resource)
{
  std::lock_guard<CCriticalSection> lock(m_resourceSection);
  m_resources.push_back(resource);
}

void CWinSystemSwitch::Unregister(IDispResource* resource)
{
  std::lock_guard<CCriticalSection> lock(m_resourceSection);
  auto it = std::find(m_resources.begin(), m_resources.end(), resource);
  if (it != m_resources.end())
    m_resources.erase(it);
}

std::unique_ptr<KODI::WINDOWING::IOSScreenSaver> CWinSystemSwitch::GetOSScreenSaverImpl()
{
  // Kodi's built-in dim/screensaver is used for now. The Switch
  // appletSetMediaPlaybackState path can replace this in the polish phase.
  return std::make_unique<KODI::WINDOWING::CDummyOSScreenSaver>();
}
