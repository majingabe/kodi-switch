/*
 *  Copyright (c) 2002 Frodo
 *      Portions Copyright (c) by the authors of ffmpeg and xvid
 *  Copyright (C) 2002-2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "Thread.h"

#include "IRunnable.h"
#include "commons/Exception.h"
#include "threads/IThreadImpl.h"
#include "threads/SingleLock.h"
#include "utils/log.h"

#include <atomic>
#include <inttypes.h>
#include <iostream>
#include <mutex>
#include <stdlib.h>
#include <vector>

#if defined(TARGET_SWITCH)
// Horizon's threadCreate fails (typically out of memory for a new stack) long
// before the platform runs out of threads, and std::thread reports that as
// std::system_error. Thrown from CThread::Create or from ~CThread (implicitly
// noexcept) it becomes std::terminate and the whole process dies mid-playback.
// Measured on Switch: kodi crash reports show
//   CJobWorker::~CJobWorker -> CThread::~CThread -> __throw_system_error
//   -> __cxa_call_terminate -> terminate
// so both places below are made non-fatal and traced.
extern "C" void switch_trace(const char* msg);
#endif

#include <cstdio>

#include <fmt/format.h>
#if FMT_VERSION >= 90000
#include <fmt/ostream.h>
#endif

#if FMT_VERSION >= 90000
template<>
struct fmt::formatter<std::thread::id> : ostream_formatter
{
};
#else
template<>
struct fmt::formatter<std::thread::id> : fmt::formatter<std::string>
{
  template<class FormatContext>
  auto format(const std::thread::id& e, FormatContext& ctx)
  {
    std::ostringstream str;
    str << e;
    return fmt::formatter<std::string>::format(str.str(), ctx);
  }
};
#endif

static thread_local CThread* currentThread;

#if defined(TARGET_SWITCH)
namespace
{
// One line per boot, so every trace in boot.log can be attributed to a build.
// boot.log is append-only and spans hundreds of launches; without this marker a
// measurement cannot be tied to the binary that produced it.
void LogBuildOnce()
{
  static std::atomic<bool> logged{false};
  if (logged.exchange(true))
    return;

  char buf[128];
  std::snprintf(buf, sizeof(buf), "kodi-switch build: threads " __DATE__ " " __TIME__);
  switch_trace(buf);
}

// pthread_detach() is a stub on this toolchain: it returns ENOSYS (88) and does
// nothing. std::thread::detach() therefore always throws std::system_error, and
// from an implicitly noexcept destructor that is std::terminate.
//
// The one place Kodi genuinely needs to detach is an auto-delete thread: a
// CJobWorker is created with Create(true), so when its job finishes the worker
// runs ~CThread on its own thread. StopThread() cannot join itself and leaves
// m_thread set, and the destructor then detaches - which threw, leaked the
// handle and (before the try/catch) killed the process mid-playback.
//
// pthread_join(), unlike detach, is implemented (it calls __syscall_thread_join)
// and reclaims the thread handle. So a self-deleting thread parks its
// std::thread here and the next Create() on another thread joins and frees it.
std::mutex g_orphanMutex;
std::vector<std::thread*> g_orphanThreads;

void ReapOrphanThreads()
{
  std::vector<std::thread*> orphans;
  {
    std::lock_guard<std::mutex> lock(g_orphanMutex);
    orphans.swap(g_orphanThreads);
  }

  for (std::thread* thread : orphans)
  {
    try
    {
      if (thread->joinable())
        thread->join(); // the owner has finished; this only reaps the handle
    }
    catch (const std::exception& e)
    {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "CThread orphan reaper: join failed: %s", e.what());
      switch_trace(buf);
    }
    catch (...)
    {
      switch_trace("CThread orphan reaper: join failed (unknown)");
    }
    delete thread;
  }
}
} // namespace
#endif

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CThread::CThread(const char* ThreadName)
:
    m_bStop(false), m_StopEvent(true, true), m_StartEvent(true, true), m_pRunnable(nullptr)
{
  if (ThreadName)
    m_ThreadName = ThreadName;
#if defined(TARGET_SWITCH)
  LogBuildOnce();
#endif
}

CThread::CThread(IRunnable* pRunnable, const char* ThreadName)
:
    m_bStop(false), m_StopEvent(true, true), m_StartEvent(true, true), m_pRunnable(pRunnable)
{
  if (ThreadName)
    m_ThreadName = ThreadName;
#if defined(TARGET_SWITCH)
  LogBuildOnce();
#endif
}

CThread::~CThread()
{
  // On Switch an exception escaping this implicitly-noexcept destructor becomes
  // std::unexpected -> std::terminate. Measured crash report (2168-0002, 1080p
  // HEVC playback at 1.00x, ~97 s in):
  //   CJobWorker::~CJobWorker -> CThread::~CThread+0x74c -> std::unexpected
  //                                                 -> __terminate
  // Root cause, read out of the linked binary: pthread_detach() on this
  // toolchain is a stub that returns ENOSYS without doing anything, so
  // std::thread::detach() always throws. An auto-delete CJobWorker destroys
  // itself from its own thread, where joining is impossible, so the destructor
  // reached detach() every time a job finished. The whole body stays covered
  // and the message is written to boot.log.
  auto destroy = [this]()
  {
    StopThread();
    if (m_thread != nullptr)
    {
#if defined(TARGET_SWITCH)
      // Still set after StopThread() means this destructor is running on the
      // thread itself (auto-delete worker), so it cannot be joined here.
      // pthread_detach is a stub returning ENOSYS, so park the object for the
      // reaper rather than throwing out of a noexcept destructor.
      {
        std::lock_guard<std::mutex> lock(g_orphanMutex);
        g_orphanThreads.push_back(m_thread);
      }
#else
      m_thread->detach();
      delete m_thread;
#endif
      m_thread = nullptr;
    }
  };

#if defined(TARGET_SWITCH)
  try
  {
    destroy();
  }
  catch (const std::exception& e)
  {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "CThread dtor escaped [%s]: %s", m_ThreadName.c_str(),
                  e.what());
    switch_trace(buf);
    if (m_thread != nullptr)
    {
      // Park it too - detach() is what threw in the first place.
      std::lock_guard<std::mutex> lock(g_orphanMutex);
      g_orphanThreads.push_back(m_thread);
      m_thread = nullptr;
    }
  }
  catch (...)
  {
    char ubuf[192];
    std::snprintf(ubuf, sizeof(ubuf), "CThread dtor escaped [%s]: unknown exception",
                  m_ThreadName.c_str());
    switch_trace(ubuf);
    if (m_thread != nullptr)
    {
      std::lock_guard<std::mutex> lock(g_orphanMutex);
      g_orphanThreads.push_back(m_thread);
    }
    m_thread = nullptr;
  }
#else
  destroy();
#endif
}

void CThread::Create(bool bAutoDelete)
{
#if defined(TARGET_SWITCH)
  // Auto-delete workers park their std::thread in ~CThread (see the note there);
  // join and free any that have finished. This runs on a different thread, which
  // is exactly what makes joining them legal.
  ReapOrphanThreads();
#endif
  if (m_thread != nullptr)
  {
    // if the thread exited on it's own, without a call to StopThread, then we can get here
    // incorrectly. We should be able to determine this by checking the promise.
#if defined(TARGET_SWITCH)
    const bool exited = m_finished.load();
#else
    std::future_status stat = m_future.wait_for(std::chrono::milliseconds(0));
    // a status of 'ready' means the future contains the value so the thread has exited
    // since the thread can't exit without setting the future.
    const bool exited = (stat == std::future_status::ready);
#endif
    if (exited) // this is an indication the thread has exited.
      StopThread(true);  // so let's just clean up
    else
    { // otherwise we have a problem.
      CLog::Log(LOGERROR, "{} - fatal error creating thread {} - old thread id not null",
                __FUNCTION__, m_ThreadName);
      switch_trace("CThread::Create: stale thread object -> exit(1)");
      exit(1);
    }
  }

  m_bAutoDelete = bAutoDelete;
  m_bStop = false;
  m_StopEvent.Reset();
  m_StartEvent.Reset();

  // lock?
  //std::unique_lock l(m_CriticalSection);

  std::promise<bool> prom;
  m_future = prom.get_future();

#if defined(TARGET_SWITCH)
  m_finished.store(false);
#endif

  {
    // The std::thread internals must be set prior to the lambda doing
    //   any work. This will cause the lambda to wait until m_thread
    //   is fully initialized. Interestingly, using a std::atomic doesn't
    //   have the appropriate memory barrier behavior to accomplish the
    //   same thing so a full system mutex needs to be used.
    std::unique_lock blockLambdaTillDone(m_CriticalSection);
#if defined(TARGET_SWITCH)
    try
    {
#endif
    m_thread = new std::thread([](CThread* pThread, std::promise<bool> promise)
    {
      try
      {

        {
          // Wait for the pThread->m_thread internals to be set. Otherwise we could
          // get to a place where we're reading, say, the thread id inside this
          // lambda's call stack prior to the thread that kicked off this lambda
          // having it set. Once this lock is released, the CThread::Create function
          // that kicked this off is done so everything should be set.
          std::unique_lock waitForThreadInternalsToBeSet(pThread->m_CriticalSection);
        }

        // This is used in various helper methods like GetCurrentThread so it needs
        // to be set before anything else is done.
        currentThread = pThread;

        if (pThread == nullptr)
        {
          CLog::Log(LOGERROR, "{}, sanity failed. thread is NULL.", __FUNCTION__);
          promise.set_value(false);
          return;
        }

        pThread->m_impl = IThreadImpl::CreateThreadImpl(pThread->m_thread->native_handle());
        pThread->m_impl->SetThreadInfo(pThread->m_ThreadName);

        CLog::Log(LOGDEBUG, "Thread {} start, auto delete: {}", pThread->m_ThreadName,
                  (pThread->m_bAutoDelete ? "true" : "false"));

        pThread->m_StartEvent.Set();

        pThread->Action();

#if defined(TARGET_SWITCH)
        // Mark completion before the auto-delete below, so IsRunning()/Join() see the
        // thread as finished even while this object is being destroyed.
        pThread->m_finished.store(true);
#endif

        if (pThread->m_bAutoDelete)
        {
          CLog::Log(LOGDEBUG, "Thread {} {} terminating (autodelete)", pThread->m_ThreadName,
                    std::this_thread::get_id());
          delete pThread;
          pThread = NULL;
        }
        else
          CLog::Log(LOGDEBUG, "Thread {} {} terminating", pThread->m_ThreadName,
                    std::this_thread::get_id());
      }
      catch (const std::exception& e)
      {
        CLog::Log(LOGDEBUG, "Thread Terminating with Exception: {}", e.what());
#if defined(TARGET_SWITCH)
        // The flag must be set however this lambda leaves, or Join()/IsRunning()
        // would never see the thread finish and StopThread() would wait forever.
        if (pThread != NULL)
          pThread->m_finished.store(true);
#endif
      }
      catch (...)
      {
        CLog::Log(LOGDEBUG,"Thread Terminating with Exception");
#if defined(TARGET_SWITCH)
        if (pThread != NULL)
          pThread->m_finished.store(true);
#endif
      }

      promise.set_value(true);
    }, this, std::move(prom));
#if defined(TARGET_SWITCH)
    }
    catch (const std::exception& e)
    {
      CLog::Log(LOGERROR, "CThread::Create failed for {}: {}", m_ThreadName, e.what());
      char tbuf[192];
      std::snprintf(tbuf, sizeof(tbuf), "CThread::Create FAILED %s: %s", m_ThreadName.c_str(),
                    e.what());
      switch_trace(tbuf);
      m_thread = nullptr;
      // StopThread() waits on this event, and ~CThread always calls it. Without
      // setting it here a failed Create would hang the destructor forever.
      m_StartEvent.Set();
    }
#endif
  } // let the lambda proceed

#if defined(TARGET_SWITCH)
  if (m_thread == nullptr)
    return; // thread creation failed; IsRunning() stays false and Join() returns false
#endif
  m_StartEvent.Wait(); // wait for the thread just spawned to set its internals
}

bool CThread::IsRunning() const
{
  if (m_thread != nullptr) {
#if defined(TARGET_SWITCH)
    // See the m_finished note in Thread.h: wait_for() throws ENOSYS here.
    return !m_finished.load();
#else
    // it's possible that the thread exited on it's own without a call to StopThread. If so then
    // the promise should be fulfilled.
    std::future_status stat = m_future.wait_for(std::chrono::milliseconds(0));
    // a status of 'ready' means the future contains the value so the thread has exited
    // since the thread can't exit without setting the future.
    if (stat == std::future_status::ready) // this is an indication the thread has exited.
      return false;
    return true; // otherwise the thread is still active.
#endif
  } else
    return false;
}

bool CThread::SetPriority(const ThreadPriority& priority)
{
  return m_impl->SetPriority(priority);
}

bool CThread::SetTask(const ThreadTask& task)
{
  return m_impl->SetTask(task);
}

bool CThread::RevertTask()
{
  return m_impl->RevertTask();
}

bool CThread::IsAutoDelete() const
{
  return m_bAutoDelete;
}

void CThread::StopThread(bool bWait /*= true*/)
{
  m_StartEvent.Wait();

  m_bStop = true;
  m_StopEvent.Set();
  std::unique_lock lock(m_CriticalSection);
  std::thread* lthread = m_thread;
  if (lthread != nullptr && bWait && !IsCurrentThread())
  {
    lock.unlock();
    if (!Join(std::chrono::milliseconds::max())) // eh?
      lthread->join();
    m_thread = nullptr;
  }
}

void CThread::Process()
{
  if (m_pRunnable)
    m_pRunnable->Run();
}

bool CThread::IsCurrentThread() const
{
  CThread* pThread = currentThread;
  if (pThread != nullptr)
    return pThread == this;
  else
    return false;
}

CThread* CThread::GetCurrentThread()
{
  return currentThread;
}

bool CThread::Join(std::chrono::milliseconds duration)
{
  std::unique_lock l(m_CriticalSection);
  std::thread* lthread = m_thread;
  if (lthread != nullptr)
  {
    if (IsCurrentThread())
      return false;

    {
      CSingleExit exit(m_CriticalSection); // don't hold the thread lock while we're waiting
#if defined(TARGET_SWITCH)
      // pthread_cond_timedwait is unimplemented on Horizon (ENOSYS), and the future's
      // wait_for() goes through it, throwing std::system_error out of this noexcept
      // caller. Poll the completion flag instead, honouring the deadline.
      using clock = std::chrono::steady_clock;
      const bool infinite = duration == std::chrono::milliseconds::max();
      const auto deadline = clock::now() + duration;
      while (!m_finished.load())
      {
        if (IsCurrentThread())
          return false;
        if (!infinite && clock::now() >= deadline)
          return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
#else
      std::future_status stat = m_future.wait_for(duration);
      if (stat != std::future_status::ready)
        return false;
#endif
    }

    // it's possible it's already joined since we released the lock above.
    if (lthread->joinable())
      m_thread->join();
    return true;
  }
  else
    return false;
}

void CThread::Action()
{
  try
  {
    OnStartup();
  }
  catch (const XbmcCommons::UncheckedException &e)
  {
    e.LogThrowMessage("OnStartup");
    if (IsAutoDelete())
      return;
  }

  try
  {
    Process();
  }
  catch (const XbmcCommons::UncheckedException &e)
  {
    e.LogThrowMessage("Process");
  }

  try
  {
    OnExit();
  }
  catch (const XbmcCommons::UncheckedException &e)
  {
    e.LogThrowMessage("OnExit");
  }
}
