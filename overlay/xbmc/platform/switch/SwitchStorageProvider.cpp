/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "storage/IStorageProvider.h"

#include <cstdlib>
#include <memory>

namespace
{
class CSwitchStorageProvider : public IStorageProvider
{
public:
  void Initialize() override {}
  void Stop() override {}

  void GetLocalDrives(std::vector<CMediaSource>& localDrives) override
  {
    CMediaSource share;
    share.strPath = "sdmc:/";
    share.strName = "SD Card";
    share.m_iDriveType = SourceType::LOCAL;
    localDrives.push_back(share);
  }

  void GetRemovableDrives(std::vector<CMediaSource>& removableDrives) override
  {
    CMediaSource share;
    share.strPath = "sdmc:/";
    share.strName = "SD Card";
    share.m_iDriveType = SourceType::REMOVABLE;
    removableDrives.push_back(share);
  }

  bool Eject(const std::string& mountpath) override { return false; }
  std::vector<std::string> GetDiskUsage() override { return {}; }
  bool PumpDriveChangeEvents(IStorageEventsCallback* callback) override { return false; }
};
} // namespace

std::unique_ptr<IStorageProvider> IStorageProvider::CreateInstance()
{
  return std::make_unique<CSwitchStorageProvider>();
}
