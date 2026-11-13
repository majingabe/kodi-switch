/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlatformPosix.h"
#include "application/AppEnvironment.h"
#include "application/AppParamParser.h"
#include "platform/xbmc.h"

#if defined(TARGET_LINUX) || defined(TARGET_FREEBSD)
#include "platform/linux/AppParamParserLinux.h"
#endif

#ifdef TARGET_WEBOS
#include "platform/linux/AppParamParserWebOS.h"
#endif

#include <cstdio>
#include <cstring>
#include <errno.h>
#include <locale.h>
#include <signal.h>

#include <sys/resource.h>

namespace
{
#if defined(TARGET_SWITCH)
extern "C" void switch_trace(const char* msg);
extern "C" void switch_preinit();
#endif

extern "C" void XBMC_POSIX_HandleSignal(int sig)
{
  // Setting an atomic flag is one of the only useful things that is permitted by POSIX
  // in signal handlers
  CPlatformPosix::RequestQuit();
}
} // namespace

int main(int argc, char* argv[])
{
#if defined(TARGET_SWITCH)
  switch_preinit();
  switch_trace("main enter");
#endif

#if defined(_DEBUG)
  struct rlimit rlim;
  rlim.rlim_cur = rlim.rlim_max = RLIM_INFINITY;
  if (setrlimit(RLIMIT_CORE, &rlim) == -1)
    fprintf(stderr, "Failed to set core size limit (%s).\n", strerror(errno));
#endif

#if defined(TARGET_SWITCH)
  switch_trace("signal handler setup");
#endif
  // Set up global SIGINT/SIGHUP/SIGTERM handler
  struct sigaction signalHandler;
  std::memset(&signalHandler, 0, sizeof(signalHandler));
  signalHandler.sa_handler = &XBMC_POSIX_HandleSignal;
#if defined(TARGET_SWITCH)
  signalHandler.sa_flags = 0;
#else
  signalHandler.sa_flags = SA_RESTART;
#endif
  sigaction(SIGINT, &signalHandler, nullptr);
  sigaction(SIGHUP, &signalHandler, nullptr);
  sigaction(SIGTERM, &signalHandler, nullptr);

  setlocale(LC_NUMERIC, "C");

#if defined(TARGET_SWITCH)
  switch_trace("parsing args");
#endif
#ifdef TARGET_WEBOS
  CAppParamParserWebOS appParamParser;
#elif defined(TARGET_LINUX) || defined(TARGET_FREEBSD)
  CAppParamParserLinux appParamParser;
#else
  CAppParamParser appParamParser;
#endif
  appParamParser.Parse(argv, argc);

#if defined(TARGET_SWITCH)
  switch_trace("CAppEnvironment::SetUp");
#endif
  CAppEnvironment::SetUp(appParamParser.GetAppParams());
#if defined(TARGET_SWITCH)
  switch_trace("XBMC_Run");
#endif
  int status = XBMC_Run(true);
#if defined(TARGET_SWITCH)
  switch_trace("XBMC_Run returned");
#endif
  CAppEnvironment::TearDown();

  return status;
}
