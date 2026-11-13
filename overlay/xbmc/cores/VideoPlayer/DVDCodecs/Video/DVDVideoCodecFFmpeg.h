/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "DVDVideoCodec.h"
#include "DVDVideoPP.h"
#include "cores/VideoPlayer/DVDCodecs/DVDCodecs.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"

#include <memory>
#include <string>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

class CVideoBufferPoolFFmpeg;

class CDVDVideoCodecFFmpeg : public CDVDVideoCodec, public ICallbackHWAccel
{
public:
  explicit CDVDVideoCodecFFmpeg(CProcessInfo &processInfo);
  ~CDVDVideoCodecFFmpeg() override;
  bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options) override;
  bool AddData(const DemuxPacket &packet) override;
  void Reset() override;
  void Reopen() override;
  CDVDVideoCodec::VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return m_name.c_str(); }; // m_name is never changed after open
  unsigned GetConvergeCount() override;
  unsigned GetAllowedReferences() override;
  bool GetCodecStats(double &pts, int &droppedFrames, int &skippedPics) override;
  void SetCodecControl(int flags) override;

  IHardwareDecoder* GetHWAccel() override;
  bool GetPictureCommon(VideoPicture* pVideoPicture) override;

protected:
  void Dispose();
  CDVDVideoCodec::VCReturn HandleHwFatal();
  static enum AVPixelFormat GetFormat(struct AVCodecContext * avctx, const AVPixelFormat * fmt);

  int  FilterOpen(const std::string& filters, bool scale);
  void FilterClose();
  CDVDVideoCodec::VCReturn FilterProcess(AVFrame* frame);
  void SetFilters();
  void UpdateName();
  bool SetPictureParams(VideoPicture* pVideoPicture);

  bool HasHardware() { return m_pHardware != nullptr; }
  void SetHardware(IHardwareDecoder *hardware);

  AVFrame* m_pFrame = nullptr;;
  AVFrame* m_pDecodedFrame = nullptr;;
#if defined(TARGET_SWITCH)
  // Tegra NVDEC: the device context is created once per decoder and the hw frames
  // are transferred to NV12 before the renderer ever sees them.
  AVBufferRef* m_hwDeviceCtx = nullptr;
  AVFrame* m_pTransferFrame = nullptr;
  // Main 10 content transfers to P010; the renderer has no 10-bit path, so it is
  // converted down to NV12 here.
  AVFrame* m_pConvertFrame = nullptr;
  SwsContext* m_pSwsCtx = nullptr;
  // True once get_format() has handed us AV_PIX_FMT_NVTEGRA. The frames are
  // transferred to NV12, so pix_fmt cannot be used to detect hardware decoding
  // (and must not be: a hardware pix_fmt without hw_frames_ctx breaks Kodi's
  // filter path). This flag is what tells ProcessInfo/DataCache "hardware".
  bool m_nvtegraHw = false;
#endif
  AVCodecContext* m_pCodecContext = nullptr;;
  std::shared_ptr<CVideoBufferPoolFFmpeg> m_videoBufferPool;

  std::string m_filters;
  std::string m_filters_next;
  AVFilterGraph* m_pFilterGraph = nullptr;
  AVFilterContext* m_pFilterIn = nullptr;
  AVFilterContext* m_pFilterOut = nullptr;;
  AVFrame* m_pFilterFrame = nullptr;;
  bool m_filterEof = false;
  bool m_eof = false;

  std::unique_ptr<IDVDVideoPP> m_postProc;

  int m_iPictureWidth = 0;
  int m_iPictureHeight = 0;
  int m_iScreenWidth = 0;
  int m_iScreenHeight = 0;
  int m_iOrientation = 0;// orientation of the video in degrees counter clockwise

  std::string m_name;
  int m_decoderState;
  int m_hwFailedCount = 0;
  IHardwareDecoder *m_pHardware = nullptr;
  int m_iLastKeyframe = 0;
  double m_dts = DVD_NOPTS_VALUE;
  bool m_started = false;
  bool m_startedInput = false;
  std::vector<AVPixelFormat> m_formats;
  double m_decoderPts = DVD_NOPTS_VALUE;
  int m_skippedDeint = 0;
  int m_droppedFrames = 0;
  bool m_requestSkipDeint = false;
  int m_codecControlFlags = 0;
  bool m_interlaced = false;
  double m_DAR = 1.0;
  CDVDStreamInfo m_hints;
  CDVDCodecOptions m_options;

  struct CDropControl
  {
    CDropControl();
    void Reset(bool init);
    void Process(int64_t pts, bool drop);

    int64_t m_lastPTS;
    int64_t m_diffPTS;
    int m_count;
    enum
    {
      INIT,
      VALID
    } m_state;
  } m_dropCtrl;
};
