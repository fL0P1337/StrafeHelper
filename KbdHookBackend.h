#pragma once

#include "InputBackend.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class KbdHookBackend final : public InputBackend {
public:
  KbdHookBackend() noexcept = default;
  ~KbdHookBackend() noexcept override;

  [[nodiscard]] bool Initialize() noexcept override;
  void Shutdown() noexcept override;
  void SetCallback(EventCallback cb) noexcept override;

  [[nodiscard]] bool InjectKey(uint16_t scanCode,
                               uint16_t flags) noexcept override;

  [[nodiscard]] bool GetStatus(BackendStatus &out) noexcept override;

  [[nodiscard]] const char *Name() const noexcept override {
    return "KbdHookBackend";
  }

  [[nodiscard]] bool CanSuppressPhysical() const noexcept override {
    return true;
  }

private:
  struct StartupState {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
  };

  static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam,
                                   LPARAM lParam) noexcept;
  void ThreadMain() noexcept;
  [[nodiscard]] static int64_t QueryQpc() noexcept;

private:
  static KbdHookBackend *instance_;

  HHOOK hook_ = nullptr;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<DWORD> threadId_{0};

  EventCallback callback_ = nullptr;

  // Per-counter atomics so the LL-hook callback never blocks on a mutex.
  // Windows enforces a hook-callback timeout (default 300 ms) and silently
  // unregisters slow hooks; even a millisecond-scale mutex stall is a real
  // risk under contention.
  std::atomic<uint32_t> sequenceCounter_{0};
  std::atomic<long> eventsCaptured_{0};
  std::atomic<long> eventsDropped_{0};
  std::atomic<long> eventsInjected_{0};

  StartupState startup_{};
};
