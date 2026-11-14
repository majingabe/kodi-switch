/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "powermanagement/IPowerSyscall.h"

class CFallbackPowerSyscall : public CPowerSyscallWithoutEvents
{
public:
  bool Powerdown() override {return true; }
  bool Suspend() override {return false; }
  bool Hibernate() override {return false; }
  bool Reboot() override {return true; }

  bool CanPowerdown() override {return true; }
  bool CanSuspend() override {return false; }
  bool CanHibernate() override {return false; }
  bool CanReboot() override {return true; }
  int  BatteryLevel() override
  {
#if defined(TARGET_SWITCH)
    // libnx exposes the charge percentage through psm; the generic fallback has no
    // battery at all, which made System info report "Battery level: 0%".
    extern "C" int SwitchBatteryLevel();
    return SwitchBatteryLevel();
#else
    return 0;
#endif
  }
};
