/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlatformSwitch.h"

#include "AESinkSwitch.h"
#include "filesystem/CurlFile.h"
#include "windowing/switch/WinSystemSwitchGLESContext.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <ctime>
#include <cxxabi.h>
#include <exception>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <switch.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

// NOTE: a __libnx_initheap() override lived here briefly (asking svcGetInfo for
// the whole process allowance instead of hbloader's ~500 MB heap). With it
// installed Kodi started and then exited on its own, before/without a crash
// screen, so it is disabled until the failure mode is understood. The heap size
// is still traced in switch_preinit below.
extern char* fake_heap_start;
extern char* fake_heap_end;

// Bring-up diagnostics. Writes straight to the SD card through libnx's devoptab
// (not Kodi's VFS) so the trace survives failures in Kodi's path setup.
//
// The file is opened and closed per message on purpose: Horizon's FAT driver
// returns EIO when a second handle is opened on a file that is still open for
// writing, so a long-lived descriptor makes boot.log unreadable while Kodi runs
// (which is exactly when it is most useful). No locks or containers, because
// this is also called from a static constructor and from signal-ish paths.
extern "C" void switch_trace(const char* msg)
{
  const int fd = ::open("sdmc:/switch/kodi/boot.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd < 0)
    return;

  ::write(fd, msg, std::strlen(msg));
  ::write(fd, "\n", 1);
  ::close(fd);

  std::printf("%s\n", msg);
  std::fflush(stdout);
}

namespace
{
// Proves that static initialisation completed far enough to reach this library.
struct EarlyBootTrace
{
  EarlyBootTrace()
  {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "static ctor reached; appletType=%d",
                  static_cast<int>(appletGetAppletType()));
    switch_trace(buf);
  }
};
EarlyBootTrace g_earlyBootTrace;
} // namespace

CPlatform* CPlatform::CreateInstance()
{
  switch_trace("CPlatform::CreateInstance");
  return new CPlatformSwitch();
}

// Must run before CAppEnvironment::SetUp(): Kodi's settings component resolves
// its data root from HOME/KODI_HOME and exits(1) if it cannot find one.
namespace
{
// libnx services this port brings up. They live in hbloader's process (an NRO
// runs inside it), so they have to be released before returning to hbmenu -
// otherwise the loader is left with half-initialised state, which is what
// produced the 2345-0008 error screen on every exit.
bool g_socketReady = false;
bool g_sslReady = false;
bool g_nifmReady = false;
bool g_hidReady = false;
} // namespace

extern "C" void switch_cleanup()
{
  switch_trace("cleanup: begin");
  if (g_sslReady)
  {
    sslExit();
    switch_trace("cleanup: sslExit");
  }
  if (g_socketReady)
  {
    socketExit();
    switch_trace("cleanup: socketExit");
  }
  if (g_nifmReady)
  {
    nifmExit();
    switch_trace("cleanup: nifmExit");
  }
  if (g_hidReady)
  {
    hidExit();
    switch_trace("cleanup: hidExit");
  }
  switch_trace("cleanup: done");
}

extern "C" void switch_preinit()
{
  switch_trace("preinit enter");

  {
    char hbuf[96];
    const auto heapBytes =
        static_cast<unsigned long long>(fake_heap_end - fake_heap_start);
    std::snprintf(hbuf, sizeof(hbuf), "heap: %.1f MB (fake_heap %p..%p)",
                  heapBytes / 1048576.0, static_cast<void*>(fake_heap_start),
                  static_cast<void*>(fake_heap_end));
    switch_trace(hbuf);
  }

  {
    static bool atexitRegistered = false;
    if (!atexitRegistered)
    {
      atexitRegistered = true;
      std::atexit(switch_cleanup);
    }
  }
  // Aborts here are otherwise undiagnosable: the Atmosphere report has no
  // symbols for the NRO and spdlog keeps up to two seconds of log unflushed.
  // Record the reason synchronously in boot.log before dying.
  {
    static bool crashHandlersInstalled = false;
    if (!crashHandlersInstalled)
    {
      crashHandlersInstalled = true;
      std::set_terminate([] {
        switch_trace("TERMINATE: std::terminate called");
        // Do NOT rethrow: rethrowing inside the terminate handler can itself
        // terminate, which is what lost the reason last time. The type name is
        // enough to tell a logic error from memory exhaustion.
        if (const std::type_info* type = abi::__cxa_current_exception_type())
        {
          char buf[256];
          std::snprintf(buf, sizeof(buf), "TERMINATE: exception type %s", type->name());
          switch_trace(buf);
        }
        else
        {
          switch_trace("TERMINATE: no active exception");
        }
        std::abort();
      });
      for (const int sig : {SIGABRT, SIGSEGV, SIGILL, SIGFPE, SIGBUS})
      {
        std::signal(sig, [](int s) {
          char buf[64];
          std::snprintf(buf, sizeof(buf), "FATAL SIGNAL: %d", s);
          switch_trace(buf);
          std::signal(s, SIG_DFL);
          std::raise(s);
        });
      }
      switch_trace("crash handlers installed");
    }
  }

  if (!getenv("HOME"))
    setenv("HOME", "/switch/kodi", 1);

  setenv("KODI_HOME", "/switch/kodi", 1);
  setenv("KODI_BIN_HOME", "/switch/kodi", 1);
  setenv("KODI_TEMP", "/switch/kodi/temp", 1);

  mkdir("sdmc:/switch/kodi", 0777);
  mkdir("sdmc:/switch/kodi/temp", 0777);
  mkdir("/switch/kodi", 0777);
  mkdir("/switch/kodi/temp", 0777);

  setenv("TMPDIR", "/switch/kodi/temp", 1);
  setenv("OS", "Switch", true);

  // Horizon has no timezone database and nothing sets TZ, so newlib's localtime()
  // reports UTC (Kodi's clock was 6 hours ahead here). Provide a POSIX TZ string;
  // /switch/kodi/timezone.txt overrides the default, e.g. "EST5EDT" or "CST6CDT".
  {
    char tz[64] = "CST6CDT";
    if (FILE* file = std::fopen("/switch/kodi/timezone.txt", "r"))
    {
      if (std::fgets(tz, sizeof(tz), file))
      {
        if (char* newline = std::strchr(tz, '\n'))
          *newline = '\0';
      }
      std::fclose(file);
    }
    if (tz[0] != '\0')
    {
      setenv("TZ", tz, 1);
      tzset();
      char buf[96];
      std::snprintf(buf, sizeof(buf), "timezone TZ=%s", tz);
      switch_trace(buf);
    }
  }

  // One-shot add-on database repair. A corrupt Addons33.db makes every repository
  // list empty ("Install from repository" shows nothing) and it cannot be repaired
  // while Kodi holds the only handle on FAT, so do it here, before the database is
  // opened. Arm it by creating /switch/kodi/repair-addons-db; the marker is removed
  // once the repair has run, so it never repeats.
  //
  // The database cannot be repaired in place: SQLite's VACUUM needs write and temp
  // support that the SD's FAT does not provide (measured: "disk I/O error", both
  // in-process and from Python on the same file). Kodi therefore recreates the
  // database empty, which silently disables every add-on. So export the enabled ids
  // first, keep the old file, and let the bundled service add-on re-apply the list
  // once the add-on manager is up (service.switch.repair).
  if (access("/switch/kodi/repair-addons-db", F_OK) == 0)
  {
    const char* db = "/switch/kodi/.kodi/userdata/Database/Addons33.db";
    const char* sidecar = "/switch/kodi/addons-enabled.txt";
    // Timestamped target: on FAT a rename onto an existing name can fail, and a
    // fixed ".corrupt" would make a second repair a silent no-op.
    char kept[160];
    std::snprintf(kept, sizeof(kept),
                  "/switch/kodi/.kodi/userdata/Database/Addons33.db.corrupt-%lld",
                  static_cast<long long>(std::time(nullptr)));

    int exported = 0;
    sqlite3* handle = nullptr;
    if (sqlite3_open_v2(db, &handle, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK)
    {
      sqlite3_stmt* stmt = nullptr;
      if (sqlite3_prepare_v2(handle, "SELECT addonID FROM installed WHERE enabled=1", -1, &stmt,
                             nullptr) == SQLITE_OK)
      {
        if (FILE* out = std::fopen(sidecar, "w"))
        {
          while (sqlite3_step(stmt) == SQLITE_ROW)
          {
            const auto* id = sqlite3_column_text(stmt, 0);
            if (id != nullptr)
            {
              std::fprintf(out, "%s\n", reinterpret_cast<const char*>(id));
              ++exported;
            }
          }
          std::fclose(out);
        }
        sqlite3_finalize(stmt);
      }
      sqlite3_close(handle);
    }

    const int renamed = std::rename(db, kept);
    std::remove("/switch/kodi/repair-addons-db");
    char rbuf[256];
    std::snprintf(rbuf, sizeof(rbuf),
                  "addons db repair: exported %d enabled id(s), rename rc=%d", exported, renamed);
    switch_trace(rbuf);
  }

  // Python: point the interpreter at the stdlib on the SD card, but only when it
  // is actually present - a bad PYTHONHOME makes Py_Initialize abort.
  {
    DIR* pythonLib = ::opendir("/switch/kodi/python/lib/python3.14");
    if (pythonLib)
    {
      ::closedir(pythonLib);
      setenv("PYTHONHOME", "/switch/kodi/python", 1);
      setenv("PYTHONPATH", "/switch/kodi/python/lib/python3.14", 1);
      switch_trace("python home set");
    }
  }

  // Python's ssl module has no compiled-in trust store and Horizon has no
  // /etc/ssl, so https would fail with CERTIFICATE_VERIFY_FAILED. Kodi ships a
  // CA bundle; use it. Only set the variable when the file exists, because
  // create_default_context() raises if SSL_CERT_FILE points at nothing.
  {
    const char* caFile = "/switch/kodi/system/certs/cacert.pem";
    if (::access(caFile, R_OK) == 0)
    {
      setenv("SSL_CERT_FILE", caFile, 1);
      setenv("REQUESTS_CA_BUNDLE", caFile, 1);
      switch_trace("ssl cert file set");
    }
  }

  // libnx does not bring up the BSD socket layer automatically; without this
  // every socket() call fails and Kodi's JSON-RPC, UPnP and curl all break.
  //
  // Measured on hardware: this process receives in 64 KB blocks, exactly one
  // block per second, while a standalone NRO using the same libnx pulls 2.2 MB/s
  // from the same server. 64 KB is libnx's *initial* tcp_rx_buf_size, so the
  // receive window is the suspect: make it large and fixed instead of letting
  // the driver start small and grow on demand.
  static bool networkInitialized = false;
  if (!networkInitialized)
  {
    networkInitialized = true;

    const SocketInitConfig* defaults = socketGetDefaultInitConfig();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "bsd defaults: tx=%u rx=%u txmax=%u rxmax=%u sb=%u sessions=%u type=%u",
                  defaults->tcp_tx_buf_size, defaults->tcp_rx_buf_size,
                  defaults->tcp_tx_buf_max_size, defaults->tcp_rx_buf_max_size,
                  defaults->sb_efficiency, defaults->num_bsd_sessions,
                  static_cast<unsigned>(defaults->bsd_service_type));
    switch_trace(buf);

    SocketInitConfig config = *defaults;
    // Known-good combination, restored after measuring: shrinking these buffers
    // does NOT buy sockets - libnx sizes the bsd pool from the requested buffer
    // sizes, so 32 KB defaults + sb=2 gave ZERO sockets where this gives two.
    // Measured with a socket gauge (open until ENOBUFS) on this config: ~2 sockets
    // available at idle. That is the real limit; see PORT-STATUS.md for what it
    // means for consecutive playbacks.
    config.tcp_rx_buf_size = 0x20000;
    config.tcp_rx_buf_max_size = 0x20000;
    config.tcp_tx_buf_size = 0x10000;
    config.tcp_tx_buf_max_size = 0x40000;
    config.sb_efficiency = 4;
    config.num_bsd_sessions = 6;

    Result rc = socketInitialize(&config);
    std::snprintf(buf, sizeof(buf), "socketInitialize(large rx) rc=0x%x (once)", rc);
    switch_trace(buf);
    g_socketReady = R_SUCCEEDED(rc);
    if (R_FAILED(rc))
    {
      const Result fallback = socketInitializeDefault();
      std::snprintf(buf, sizeof(buf), "socketInitializeDefault fallback rc=0x%x", fallback);
      switch_trace(buf);
      g_socketReady = R_SUCCEEDED(fallback);
    }

    // Measure the budget instead of guessing it. libnx does not expose the pool
    // size, and shrinking the requested buffers has been observed to *reduce* the
    // number of usable sockets, so the only reliable number comes from opening
    // sockets until the service refuses.
    if (g_socketReady)
    {
      std::vector<int> gauge;
      int fd = 0;
      while (gauge.size() < 64 && (fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0)
        gauge.push_back(fd);
      const int lastErrno = errno;
      for (int s : gauge)
        close(s);
      std::snprintf(buf, sizeof(buf), "socket gauge: %zu usable (stop errno=%d, cfg rx=%u tx=%u sb=%u sessions=%u)",
                    gauge.size(), lastErrno, config.tcp_rx_buf_size, config.tcp_tx_buf_size,
                    config.sb_efficiency, config.num_bsd_sessions);
      switch_trace(buf);
    }

  }
  g_nifmReady = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));

  // ffmpeg's libnx TLS backend (libavformat/tls_libnx.c, which provides https://)
  // talks to the ssl service, and that has to be brought up explicitly. Kodi's
  // own HTTPS goes through OpenSSL/curl and does not need this, which is why it
  // worked while ffmpeg's https did not.
  {
    static bool sslInitialized = false;
    if (!sslInitialized)
    {
      sslInitialized = true;
      const Result rc = sslInitialize(2);
      char buf[96];
      std::snprintf(buf, sizeof(buf), "sslInitialize rc=0x%x", rc);
      switch_trace(buf);
      g_sslReady = R_SUCCEEDED(rc);
    }
  }

  // The pad API reads through HID; without this every padUpdate() reports
  // "no controller connected" and Kodi receives no input at all.
  {
    char buf[128];
    const Result rc = hidInitialize();
    g_hidReady = R_SUCCEEDED(rc);
    // A process only receives controller input for the styles it declares, so
    // declare the standard set explicitly (hbmenu does this for itself; an
    // application launched from it inherits nothing).
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    std::snprintf(buf, sizeof(buf), "hidInitialize rc=0x%x mode=%d npad=0x%x", rc,
                  static_cast<int>(appletGetOperationMode()),
                  hidGetNpadStyleSet(HidNpadIdType_No1));
    switch_trace(buf);
  }

  switch_trace("preinit done");
}

bool CPlatformSwitch::InitStageOne()
{
  switch_trace("InitStageOne enter");

  switch_preinit();

  switch_trace("before CPlatformPosix::InitStageOne");
  if (!CPlatformPosix::InitStageOne())
  {
    switch_trace("CPlatformPosix::InitStageOne FAILED");
    return false;
  }
  switch_trace("after CPlatformPosix::InitStageOne");

  // curl is built --without-ca-bundle/--without-ca-path because Horizon has no
  // system trust store, and Kodi only preloads its bundled CA file on Win10.
  // Without this every https:// URL fails verification, which breaks scrapers,
  // streaming add-ons and any remote media. Same call Win10 makes.
  XFILE::CCurlFile::PreloadCaCertsBlob();
  switch_trace("ca certs preloaded");

  // Register the audout-backed audio sink before ActiveAE enumerates sinks.
  CAESinkSwitch::Register();
  switch_trace("audio sink registered");

  CWinSystemSwitchGLESContext::Register();
  switch_trace("InitStageOne done");
  return true;
}

// -----------------------------------------------------------------------------
// Horizon OS has no dynamic linker, so Kodi cannot dlopen() binary add-ons.
// inputstream.adaptive is built as a static library and linked into kodi.bin;
// these satisfy the three symbols CAddonDll resolves (see AddonDll.h) when the
// add-on manager asks to load it. Anything else still fails, exactly as before.
//
// They are named switch_* because the platform's <dlfcn.h> provides inline
// stubs called dlopen/dlsym/dlerror; SoLoader.cpp is redirected to these.
extern "C" int ADDON_Create(void* addonInterface);
extern "C" const char* ADDON_GetTypeVersion(int type);
extern "C" const char* ADDON_GetTypeMinVersion(int type);

// pvr.iptvsimple is built with prefixed entry points (see the build script) so both
// static add-ons can live in one binary without colliding on ADDON_Create.
extern "C" int PVR_ADDON_Create(void* addonInterface);
extern "C" const char* PVR_ADDON_GetTypeVersion(int type);
extern "C" const char* PVR_ADDON_GetTypeMinVersion(int type);

namespace
{
char g_dlLastError[256];
int g_staticAddonHandle;
int g_pvrAddonHandle;

// inputstream.adaptive can throw while opening a manifest/segment, and an
// exception escaping through ADDON_Create reaches Kodi's C boundary where it
// cannot unwind: the process calls std::terminate and the console dies. Catch it
// here and report an ordinary add-on failure instead.
int SwitchAddonCreate(void* addonInterface)
{
  try
  {
    const int status = ADDON_Create(addonInterface);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "ADDON_Create returned %d", status);
    switch_trace(buf);
    return status;
  }
  catch (const std::exception& ex)
  {
    char buf[400];
    std::snprintf(buf, sizeof(buf), "ADDON_Create threw: %s", ex.what());
    switch_trace(buf);
  }
  catch (...)
  {
    switch_trace("ADDON_Create threw a non-standard exception");
  }
  return 5; // ADDON_STATUS_PERMANENT_FAILURE
}

void* LookupStaticAddonSymbol(const char* name)
{
  if (name == nullptr)
    return nullptr;

  if (std::strcmp(name, "ADDON_Create") == 0)
    return reinterpret_cast<void*>(&SwitchAddonCreate);
  if (std::strcmp(name, "ADDON_GetTypeVersion") == 0)
    return reinterpret_cast<void*>(&ADDON_GetTypeVersion);
  if (std::strcmp(name, "ADDON_GetTypeMinVersion") == 0)
    return reinterpret_cast<void*>(&ADDON_GetTypeMinVersion);

  return nullptr;
}

// pvr.iptvsimple entry points, with the same exception guard as ISA: a throw across
// the C boundary terminates the process.
int SwitchPvrAddonCreate(void* addonInterface)
{
  try
  {
    const int status = PVR_ADDON_Create(addonInterface);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "PVR_ADDON_Create returned %d", status);
    switch_trace(buf);
    return status;
  }
  catch (const std::exception& ex)
  {
    char buf[400];
    std::snprintf(buf, sizeof(buf), "PVR_ADDON_Create threw: %s", ex.what());
    switch_trace(buf);
  }
  catch (...)
  {
    switch_trace("PVR_ADDON_Create threw a non-standard exception");
  }
  return 5; // ADDON_STATUS_PERMANENT_FAILURE
}

void* LookupPvrAddonSymbol(const char* name)
{
  if (name == nullptr)
    return nullptr;

  if (std::strcmp(name, "ADDON_Create") == 0)
    return reinterpret_cast<void*>(&SwitchPvrAddonCreate);
  if (std::strcmp(name, "ADDON_GetTypeVersion") == 0)
    return reinterpret_cast<void*>(&PVR_ADDON_GetTypeVersion);
  if (std::strcmp(name, "ADDON_GetTypeMinVersion") == 0)
    return reinterpret_cast<void*>(&PVR_ADDON_GetTypeMinVersion);

  return nullptr;
}
} // namespace

extern "C" void* switch_dlopen(const char* file, int mode)
{
  (void)mode;

  if (file != nullptr && std::strstr(file, "inputstream.adaptive") != nullptr)
    return &g_staticAddonHandle;
  if (file != nullptr && std::strstr(file, "pvr.iptvsimple") != nullptr)
    return &g_pvrAddonHandle;

  std::snprintf(g_dlLastError, sizeof(g_dlLastError),
                "no dynamic linker on Horizon OS: %s", file ? file : "(null)");
  return nullptr;
}

extern "C" void* switch_dlsym(void* handle, const char* name)
{
  void* symbol = nullptr;
  if (handle == &g_staticAddonHandle)
    symbol = LookupStaticAddonSymbol(name);
  else if (handle == &g_pvrAddonHandle)
    symbol = LookupPvrAddonSymbol(name);
  else
  {
    std::snprintf(g_dlLastError, sizeof(g_dlLastError), "invalid library handle");
    return nullptr;
  }

  if (symbol == nullptr)
    std::snprintf(g_dlLastError, sizeof(g_dlLastError), "symbol not found: %s",
                  name ? name : "(null)");
  return symbol;
}

extern "C" int switch_dlclose(void* handle)
{
  (void)handle;
  return 0;
}

extern "C" char* switch_dlerror()
{
  return g_dlLastError[0] != '\0' ? g_dlLastError : nullptr;
}
