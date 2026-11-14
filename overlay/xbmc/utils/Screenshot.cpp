/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Screenshot.h"

#include "ServiceBroker.h"
#include "URL.h"
#include "Util.h"
#include "guilib/TextureFormats.h"
#include "pictures/Picture.h"
#include "rendering/capture/CaptureHandle.h"
#include "rendering/capture/CapturePixels.h"
#include "rendering/capture/CaptureService.h"
#include "rendering/capture/CaptureTypes.h"
#include "resources/LocalizeStrings.h"
#include "resources/ResourcesComponent.h"
#include "settings/SettingPath.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/windows/GUIControlSettings.h"
#include "utils/URIUtils.h"
#include "utils/log.h"

#include <memory>
#include <cstring>
#include <vector>

#if defined(TARGET_SWITCH)
#include <GLES2/gl2.h>

#include "windowing/WinSystem.h"
#endif

namespace
{
#if defined(TARGET_SWITCH)
/*! \brief Framebuffer readback for Horizon.
 *
 * Kodi 22 screenshots go through CCaptureService, but only the DRM-PRIME
 * renderer implements a capture provider; LinuxRendererGLES does not, so
 * Submit() never completes and TakeScreenshot silently writes nothing. Read the
 * default framebuffer directly instead and hand it to the normal picture writer.
 *
 * Called on the application thread, where the GL context is current.
 */
void TakeScreenshotSwitch(const std::string& filename)
{
  const int width = CServiceBroker::GetWinSystem()->GetWidth();
  const int height = CServiceBroker::GetWinSystem()->GetHeight();
  if (width <= 0 || height <= 0)
  {
    CLog::Log(LOGERROR, "Screenshot {} failed: window is {}x{}", CURL::GetRedacted(filename),
              width, height);
    return;
  }

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  GLint previous = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous));

  // GL's origin is bottom-left, image writers expect top-left.
  std::vector<uint8_t> flipped(pixels.size());
  const size_t rowBytes = static_cast<size_t>(width) * 4;
  for (int y = 0; y < height; ++y)
    std::memcpy(&flipped[static_cast<size_t>(y) * rowBytes],
                &pixels[static_cast<size_t>(height - 1 - y) * rowBytes], rowBytes);

  if (!CPicture::CreateThumbnailFromSurface(flipped.data(), width, height, static_cast<int>(rowBytes),
                                            filename, XB_FMT_RGBA8 | XB_FMT_OPAQUE))
    CLog::Log(LOGERROR, "Screenshot {} failed to encode", CURL::GetRedacted(filename));
  else
    CLog::Log(LOGINFO, "Screenshot written: {}", CURL::GetRedacted(filename));
}
#endif
} // namespace

std::vector<std::function<std::unique_ptr<IScreenshotSurface>()>> CScreenShot::m_screenShotSurfaces;

void CScreenShot::Register(const std::function<std::unique_ptr<IScreenshotSurface>()>& createFunc)
{
  m_screenShotSurfaces.emplace_back(createFunc);
}

std::unique_ptr<IScreenshotSurface> CScreenShot::CreateSurface()
{
  if (m_screenShotSurfaces.empty())
    return {};
  return m_screenShotSurfaces.back()();
}

namespace
{
// Map a capture's source coding to the encoder's XB_FMT input format.
unsigned int CaptureXbFormat(AVPixelFormat format)
{
  switch (format)
  {
    case AV_PIX_FMT_BGRA:
      return XB_FMT_A8R8G8B8;
    case AV_PIX_FMT_RGBA:
      return XB_FMT_RGBA8;
    case AV_PIX_FMT_RGBA64LE:
      return XB_FMT_RGBA16;
    case AV_PIX_FMT_X2BGR10LE:
      return XB_FMT_X2BGR10;
    default:
      return XB_FMT_UNKNOWN;
  }
}

// Encode a delivered capture to the destination file; runs on the capture
// service's callback worker.
void WriteCapture(const KODI::RENDERING::CAPTURE::CaptureResult& result,
                  const std::string& filename)
{
  using namespace KODI::RENDERING::CAPTURE;
  if (!result.pixels)
  {
    CLog::Log(LOGERROR, "Unable to write screenshot {}: no pixels", CURL::GetRedacted(filename));
    return;
  }

  CScopedCapturePixels lock(*result.pixels);
  if (!lock.data())
  {
    CLog::Log(LOGERROR, "Unable to write screenshot {}: pixel lock failed",
              CURL::GetRedacted(filename));
    return;
  }

  // OPAQUE: the encoder drops the framebuffer alpha through an X-variant source
  const unsigned int format = CaptureXbFormat(result.format) | XB_FMT_OPAQUE;
  const uint8_t* src0 = CaptureSrcRow0(lock.data(), result.stride, result.height);

  if (!CPicture::CreateThumbnailFromSurface(src0, result.width, result.height,
                                            static_cast<unsigned int>(result.stride), filename,
                                            format, result.color))
    CLog::Log(LOGERROR, "Unable to write screenshot {}", CURL::GetRedacted(filename));
}
} // namespace

void CScreenShot::TakeScreenshot(const std::string& filename,
                                 KODI::RENDERING::CAPTURE::CaptureContent content)
{
#if defined(TARGET_SWITCH)
  (void)content; // only the composited image is available on this platform
  TakeScreenshotSwitch(filename);
  return;
#endif

  using namespace KODI::RENDERING::CAPTURE;

  const auto captureService = CServiceBroker::GetCaptureService();
  if (!captureService)
  {
    CLog::Log(LOGERROR, "Screenshot {} failed: no capture service", CURL::GetRedacted(filename));
    return;
  }

  CLog::Log(LOGDEBUG, "Saving screenshot {}", CURL::GetRedacted(filename));

  // screenshots are tagged captures whatever their content: native depth
  // kept, the display's coding carried as cICP by the writer
  CaptureSpec spec;
  spec.content = content;
  spec.format = CaptureFormat::NATIVE;

  // failure dispatches an empty result because some consumers must act on
  // it (a bookmark still writes its DB row); here WriteCapture declines it
  auto handle = captureService->Submit(spec, [filename](const CaptureResult& result)
                                       { WriteCapture(result, filename); });
  handle->Detach();
}

namespace
{
// Resolve the configured screenshot folder, prompting for it once if unset.
std::string ResolveScreenshotDir()
{
  std::shared_ptr<CSettingPath> setting = std::static_pointer_cast<CSettingPath>(
      CServiceBroker::GetSettingsComponent()->GetSettings()->GetSetting(
          CSettings::SETTING_DEBUG_SCREENSHOTPATH));
  if (!setting)
    return {};

  std::string dir = setting->GetValue();
  if (dir.empty())
  {
    if (!CGUIControlButtonSetting::GetPath(
            setting, &CServiceBroker::GetResourcesComponent().GetLocalizeStrings()))
      return {};
    dir = setting->GetValue();
  }

  URIUtils::RemoveSlashAtEnd(dir);
  return dir;
}
} // namespace

void CScreenShot::TakeScreenshot()
{
  TakeScreenshot(KODI::RENDERING::CAPTURE::CaptureContent::COMPOSITE);
}

void CScreenShot::TakeScreenshot(KODI::RENDERING::CAPTURE::CaptureContent content)
{
#if defined(TARGET_SWITCH)
  // The other overload was already bypassed for Horizon, but the screenshot
  // action comes through here, which still used CCaptureService. LinuxRendererGLES
  // registers no capture provider, so Submit() never completed and the action
  // silently wrote nothing. Resolve the folder and read the framebuffer directly,
  // exactly like the filename overload does.
  (void)content;
  const std::string dir = ResolveScreenshotDir();
  if (dir.empty())
  {
    CLog::Log(LOGWARNING, "No screenshot path configured");
    return;
  }
  const std::string file =
      CUtil::GetNextFilename(URIUtils::AddFileToFolder(dir, "screenshot{:05}.png"), 65535);
  if (file.empty())
  {
    CLog::Log(LOGWARNING, "Too many screen shots or invalid folder");
    return;
  }
  TakeScreenshotSwitch(file);
  return;
#endif

  using namespace KODI::RENDERING::CAPTURE;

  const auto captureService = CServiceBroker::GetCaptureService();
  if (!captureService)
    return;

  CaptureSpec spec;
  spec.content = content;
  spec.format = CaptureFormat::NATIVE;

  // Submitted before any filesystem work so the readback grabs the current frame;
  // the folder dialog (when the path is unset), naming, and write all run in the
  // callback afterward, so none of them delay the capture.
  auto handle =
      captureService->Submit(spec,
                             [](const CaptureResult& result)
                             {
                               if (!result.pixels)
                                 return;
                               const std::string dir = ResolveScreenshotDir();
                               if (dir.empty())
                               {
                                 CLog::Log(LOGWARNING, "No screenshot path configured");
                                 return;
                               }
                               const std::string file = CUtil::GetNextFilename(
                                   URIUtils::AddFileToFolder(dir, "screenshot{:05}.png"), 65535);
                               if (file.empty())
                               {
                                 CLog::Log(LOGWARNING, "Too many screen shots or invalid folder");
                                 return;
                               }
                               CLog::Log(LOGDEBUG, "Saving screenshot {}", CURL::GetRedacted(file));
                               WriteCapture(result, file);
                             });
  handle->Detach();
}

void CScreenShot::TakeScreenshotBoth()
{
  using namespace KODI::RENDERING::CAPTURE;

  const auto captureService = CServiceBroker::GetCaptureService();
  if (!captureService)
    return;

  auto composite = std::make_shared<std::string>();
  CaptureSpec spec;
  spec.content = CaptureContent::BOTH;
  spec.format = CaptureFormat::NATIVE;
  auto handle = captureService->Submit(
      spec,
      [composite](const CaptureResult& result)
      {
        if (!result.pixels)
          return;
        if (composite->empty())
        {
          const std::string dir = ResolveScreenshotDir();
          if (dir.empty())
          {
            CLog::Log(LOGWARNING, "No screenshot path configured");
            return;
          }
          *composite =
              CUtil::GetNextFilename(URIUtils::AddFileToFolder(dir, "screenshot{:05}.png"), 65535);
        }
        if (composite->empty())
        {
          CLog::Log(LOGWARNING, "Too many screen shots or invalid folder");
          return;
        }
        // derive the video name from the same NNNNN so the pair is obvious
        const std::string file = result.content == CaptureContent::VIDEO
                                     ? composite->substr(0, composite->length() - 4) + "-video.png"
                                     : *composite;
        CLog::Log(LOGDEBUG, "Saving screenshot {}", CURL::GetRedacted(file));
        WriteCapture(result, file);
      });
  handle->Detach();
}
