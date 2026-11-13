/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

// Horizon OS shims.
//
// newlib/libnx on the Switch do not implement pipe() and do not support
// AF_UNIX socketpair(). libmicrohttpd (Kodi's web server) builds its
// inter-thread wake channel with socketpair(AF_UNIX, SOCK_STREAM) and needs the
// pair to work with send()/recv()/select(), so without this MHD_start_daemon()
// fails for every threaded mode and Kodi logs
// "CWebserver[8080]: Failed to start".
//
// A connected pair of loopback TCP sockets behaves identically for that use.

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <switch.h>
#include <unistd.h>

namespace
{
int MakeLoopbackPair(int sv[2])
{
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0)
    return -1;

  struct sockaddr_in addr
  {
  };
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (bind(listener, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(listener, 1) != 0)
  {
    close(listener);
    return -1;
  }

  socklen_t len = sizeof(addr);
  if (getsockname(listener, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0)
  {
    close(listener);
    return -1;
  }

  const int writer = socket(AF_INET, SOCK_STREAM, 0);
  if (writer < 0 || connect(writer, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0)
  {
    if (writer >= 0)
      close(writer);
    close(listener);
    return -1;
  }

  const int reader = accept(listener, nullptr, nullptr);
  close(listener);
  if (reader < 0)
  {
    close(writer);
    return -1;
  }

  sv[0] = reader;
  sv[1] = writer;
  return 0;
}
} // namespace

extern "C" int pipe(int fds[2])
{
  return MakeLoopbackPair(fds);
}

extern "C" int pipe2(int fds[2], int /*flags*/)
{
  return MakeLoopbackPair(fds);
}

extern "C" int kodi_switch_socket_pair(int sv[2])
{
  return MakeLoopbackPair(sv);
}

extern "C" int kodi_switch_socket_pair_nblk(int sv[2])
{
  if (MakeLoopbackPair(sv) != 0)
    return -1;

  for (int i = 0; i < 2; ++i)
  {
    const int flags = fcntl(sv[i], F_GETFL, 0);
    if (flags >= 0)
      fcntl(sv[i], F_SETFL, flags | O_NONBLOCK);
  }
  return 0;
}

// newlib has no pause(); CPython's thread code uses it only to park a thread.
extern "C" int pause(void)
{
  for (;;)
    svcSleepThread(1000000000ull);
  return -1;
}
