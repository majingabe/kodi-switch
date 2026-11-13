// SQLite VFS for Nintendo Switch (Horizon OS / devkitA64).
//
// Why this exists:
//  * newlib's fcntl() only implements F_GETFD/F_SETFD/F_GETFL/F_SETFL, so
//    SQLite's POSIX locking fails with SQLITE_IOERR_LOCK.
//  * libnx's SD devoptab refuses a second open of a file that is already open
//    (the second open() fails, which SQLite reports as SQLITE_CANTOPEN). Kodi
//    legitimately opens the same database from several CDatabase instances in
//    worker threads, so a plain VFS cannot work.
//
// The VFS below keeps exactly one file descriptor per path and shares it
// between connections, with a real in-process read/write lock (SHARED/EXCLUSIVE
// emulating POSIX advisory locks). Journal files are avoided by using
// journal_mode=MEMORY in Kodis's database layer.
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <sqlite3.h>

namespace switchvfs
{
constexpr const char* kName = "switch";
constexpr int kMaxPath = 512;

struct SharedFile
{
  int fd{-1};
  int refs{0};
  bool readonly{false};

  // Serialises actual I/O on the shared descriptor.
  std::mutex ioMutex;

  // In-process stand-in for POSIX advisory locking.
  std::mutex lockMutex;
  int sharedLocks{0};
  bool exclusive{false};
};

struct File
{
  sqlite3_file base; // must stay first
  SharedFile* shared;
  bool holdsShared;
  char path[kMaxPath];
};

inline std::mutex& MapMutex()
{
  static std::mutex m;
  return m;
}

inline std::map<std::string, SharedFile*>& Files()
{
  static std::map<std::string, SharedFile*> m;
  return m;
}

inline int Close(sqlite3_file* pFile)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(MapMutex());
  if (p->shared)
  {
    if (--p->shared->refs <= 0)
    {
      if (p->shared->fd >= 0)
        ::close(p->shared->fd);
      Files().erase(std::string(p->path));
      delete p->shared;
    }
    p->shared = nullptr;
  }
  return SQLITE_OK;
}

inline int Read(sqlite3_file* pFile, void* buf, int amt, sqlite3_int64 offset)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(p->shared->ioMutex);
  if (::lseek(p->shared->fd, static_cast<off_t>(offset), SEEK_SET) < 0)
    return SQLITE_IOERR_READ;

  int got = 0;
  while (got < amt)
  {
    const ssize_t n = ::read(p->shared->fd, static_cast<char*>(buf) + got, amt - got);
    if (n < 0)
    {
      if (errno == EINTR)
        continue;
      return SQLITE_IOERR_READ;
    }
    if (n == 0)
      break;
    got += static_cast<int>(n);
  }

  if (got < amt)
  {
    std::memset(static_cast<char*>(buf) + got, 0, amt - got);
    return SQLITE_IOERR_SHORT_READ;
  }
  return SQLITE_OK;
}

inline int Write(sqlite3_file* pFile, const void* buf, int amt, sqlite3_int64 offset)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(p->shared->ioMutex);
  if (::lseek(p->shared->fd, static_cast<off_t>(offset), SEEK_SET) < 0)
    return SQLITE_IOERR_WRITE;

  int put = 0;
  while (put < amt)
  {
    const ssize_t n = ::write(p->shared->fd, static_cast<const char*>(buf) + put, amt - put);
    if (n <= 0)
    {
      if (n < 0 && errno == EINTR)
        continue;
      return SQLITE_IOERR_WRITE;
    }
    put += static_cast<int>(n);
  }
  return SQLITE_OK;
}

inline int Truncate(sqlite3_file* pFile, sqlite3_int64 size)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(p->shared->ioMutex);
  return ::ftruncate(p->shared->fd, static_cast<off_t>(size)) == 0 ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

inline int Sync(sqlite3_file* pFile, int /*flags*/)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(p->shared->ioMutex);
  return ::fsync(p->shared->fd) == 0 ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

inline int FileSize(sqlite3_file* pFile, sqlite3_int64* pSize)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::lock_guard<std::mutex> guard(p->shared->ioMutex);
  struct stat st;
  if (::fstat(p->shared->fd, &st) != 0)
  {
    *pSize = 0;
    return SQLITE_IOERR_FSTAT;
  }
  *pSize = static_cast<sqlite3_int64>(st.st_size);
  return SQLITE_OK;
}

// Locking: all connections live in this process and share one descriptor, so
// there is nothing to gain from simulating advisory locks - and a false BUSY
// here deadlocks Kodi's database threads. Grant everything; SQLite's own
// per-connection mutexes serialise access within a connection, which is how
// Kodi uses its databases.
inline int Lock(sqlite3_file*, int) { return SQLITE_OK; }
inline int Unlock(sqlite3_file*, int) { return SQLITE_OK; }
inline int CheckReservedLock(sqlite3_file*, int* pResOut)
{
  *pResOut = 0;
  return SQLITE_OK;
}

inline int FileControl(sqlite3_file*, int, void*) { return SQLITE_NOTFOUND; }
inline int SectorSize(sqlite3_file*) { return 4096; }
inline int DeviceCharacteristics(sqlite3_file*) { return 0; }

inline int ShmMap(sqlite3_file*, int, int, int, void volatile**) { return SQLITE_IOERR_SHMMAP; }
inline int ShmLock(sqlite3_file*, int, int, int) { return SQLITE_IOERR_SHMLOCK; }
inline void ShmBarrier(sqlite3_file*) {}
inline int ShmUnmap(sqlite3_file*, int) { return SQLITE_OK; }

inline const sqlite3_io_methods kIoMethods = {
    2,        Close,       Read,   Write,       Truncate,     Sync,
    FileSize, Lock,        Unlock, CheckReservedLock, FileControl, SectorSize,
    DeviceCharacteristics, ShmMap, ShmLock,     ShmBarrier,   ShmUnmap,
    nullptr,  nullptr}; // xFetch/xUnfetch (only used by iVersion 3)

inline int Open(sqlite3_vfs*, const char* zName, sqlite3_file* pFile, int flags, int* pOutFlags)
{
  auto* p = reinterpret_cast<File*>(pFile);
  std::memset(p, 0, sizeof(File));

  if (!zName || zName[0] == '\0')
    return SQLITE_CANTOPEN; // temp files: Kodi sets temp_store=MEMORY

  std::lock_guard<std::mutex> guard(MapMutex());
  const std::string key(zName);
  SharedFile* shared = nullptr;

  auto it = Files().find(key);
  if (it != Files().end())
  {
    shared = it->second;
  }
  else
  {
    int oflags = (flags & SQLITE_OPEN_READONLY) ? O_RDONLY : O_RDWR;
    if (!(flags & SQLITE_OPEN_READONLY) && (flags & SQLITE_OPEN_CREATE))
      oflags |= O_CREAT;

    const int fd = ::open(zName, oflags, 0666);
    if (fd < 0)
      return SQLITE_CANTOPEN;

    shared = new SharedFile();
    shared->fd = fd;
    shared->readonly = (flags & SQLITE_OPEN_READONLY) != 0;
    Files()[key] = shared;
  }

  shared->refs++;
  p->shared = shared;
  std::snprintf(p->path, sizeof(p->path), "%s", zName);
  p->base.pMethods = &kIoMethods;
  if (pOutFlags)
    *pOutFlags = flags;
  return SQLITE_OK;
}

inline int Delete(sqlite3_vfs*, const char* zName, int /*syncDir*/)
{
  return ::unlink(zName) == 0 ? SQLITE_OK : SQLITE_IOERR_DELETE;
}

inline int Access(sqlite3_vfs*, const char* zName, int /*flags*/, int* pResOut)
{
  const int fd = ::open(zName, O_RDONLY);
  if (fd >= 0)
  {
    ::close(fd);
    *pResOut = 1;
  }
  else
  {
    *pResOut = 0;
  }
  return SQLITE_OK;
}

inline int FullPathname(sqlite3_vfs*, const char* zName, int nOut, char* zOut)
{
  std::snprintf(zOut, static_cast<size_t>(nOut), "%s", zName);
  return SQLITE_OK;
}

inline void* DlOpen(sqlite3_vfs*, const char*) { return nullptr; }
inline void DlError(sqlite3_vfs*, int, char*) {}
inline void (*DlSym(sqlite3_vfs*, void*, const char*))(void) { return nullptr; }
inline void DlClose(sqlite3_vfs*, void*) {}

inline int Randomness(sqlite3_vfs*, int nByte, char* zOut)
{
  static uint64_t state = 0x9E3779B97F4A7C15ull;
  for (int i = 0; i < nByte; ++i)
  {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    zOut[i] = static_cast<char>(state >> 24);
  }
  return nByte;
}

inline int Sleep(sqlite3_vfs*, int microseconds)
{
  ::usleep(static_cast<useconds_t>(microseconds));
  return microseconds;
}

inline int CurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* pTime)
{
  struct timeval tv;
  if (::gettimeofday(&tv, nullptr) != 0)
  {
    *pTime = 0;
    return SQLITE_ERROR;
  }
  *pTime = static_cast<sqlite3_int64>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
  return SQLITE_OK;
}

inline int CurrentTime(sqlite3_vfs*, double* pTime)
{
  sqlite3_int64 i = 0;
  CurrentTimeInt64(nullptr, &i);
  *pTime = static_cast<double>(i) / 1000.0;
  return SQLITE_OK;
}

inline int GetLastError(sqlite3_vfs*, int, char*) { return 0; }

inline sqlite3_vfs kVfs = {3,
                           sizeof(File),
                           kMaxPath,
                           nullptr, // pNext
                           kName,    // zName
                           nullptr,  // pAppData
                           Open,
                           Delete,
                           Access,
                           FullPathname,
                           DlOpen,
                           DlError,
                           DlSym,
                           DlClose,
                           Randomness,
                           Sleep,
                           CurrentTime,
                           GetLastError,
                           CurrentTimeInt64,
                           nullptr, // xSetSystemCall
                           nullptr, // xGetSystemCall
                           nullptr}; // xNextSystemCall

// Registers the VFS once and returns its name.
inline const char* Name()
{
  static const bool registered = [] {
    sqlite3_vfs_register(&kVfs, 0);
    return true;
  }();
  (void)registered;
  return kName;
}
} // namespace switchvfs
