/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "network/Network.h"
#include "platform/posix/network/NetworkPosix.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <switch.h>

namespace
{
std::string IpToString(u32 addr)
{
  // nifm returns the address in network byte order.
  const unsigned char* b = reinterpret_cast<const unsigned char*>(&addr);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return buf;
}

bool SwitchNetworkConnected()
{
  NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
  if (R_FAILED(nifmGetInternetConnectionStatus(nullptr, nullptr, &status)))
    return false;
  return status == NifmInternetConnectionStatus_Connected;
}

u32 SwitchIpAddress()
{
  u32 addr = 0;
  if (R_FAILED(nifmGetCurrentIpAddress(&addr)))
    return 0;
  return addr;
}

// Minimal CNetworkInterface for Horizon: Kodi gates every network service on
// "is there a connected interface", so this must exist and report the console's
// wlan/eth address from nifm.
class CNetworkInterfaceSwitch : public CNetworkInterface
{
public:
  CNetworkInterfaceSwitch() = default;
  ~CNetworkInterfaceSwitch() override = default;

  bool IsEnabled() const override { return true; }
  bool IsConnected() const override { return SwitchNetworkConnected(); }

  std::string GetMacAddress() const override { return "02:00:00:00:00:00"; }
  void GetMacAddressRaw(char rawMac[6]) const override { std::memset(rawMac, 0, 6); }
  bool GetHostMacAddress(unsigned long /*host*/, std::string& /*mac*/) const override
  {
    return false;
  }

  std::string GetCurrentIPAddress() const override { return IpToString(SwitchIpAddress()); }
  std::string GetCurrentNetmask() const override { return "255.255.255.0"; }
  std::string GetCurrentDefaultGateway() const override
  {
    const u32 addr = SwitchIpAddress();
    if (!addr)
      return "";
    unsigned char* b = const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(&addr));
    b[3] = 1; // x.y.z.1
    return IpToString(addr);
  }
};

class CNetworkSwitch : public CNetworkPosix
{
public:
  CNetworkSwitch() { queryInterfaceList(); }
  ~CNetworkSwitch() override
  {
    for (auto* iface : m_interfaces)
      delete iface;
    m_interfaces.clear();
  }

protected:
  void GetMacAddress(const std::string& /*interfaceName*/, char macAddrRaw[6]) override
  {
    std::memset(macAddrRaw, 0, 6);
  }

  void queryInterfaceList() override
  {
    for (auto* iface : m_interfaces)
      delete iface;
    m_interfaces.clear();
    m_interfaces.push_back(new CNetworkInterfaceSwitch());
  }

public:
  bool PingHost(unsigned long host, unsigned int timeout_ms = 2000) override { return false; }
  std::vector<std::string> GetNameServers() override { return {}; }
};
} // namespace

std::unique_ptr<CNetworkBase> CNetworkBase::GetNetwork()
{
  return std::make_unique<CNetworkSwitch>();
}
