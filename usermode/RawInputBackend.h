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

class RawInputBackend final : public InputBackend {
public:
  RawInputBackend() noexcept = default;
  ~RawInputBackend() noexcept override;

  [[nodiscard]] bool Initialize() noexcept override;
  void Shutdown() noexcept override;

  [[nodiscard]] uint32_t PollEvents(NEO_KEY_EVENT *out,
                                    uint32_t maxCount) noexcept override;
  [[nodiscard]] bool PassThrough(const NEO_KEY_EVENT &event) noexcept override;
  [[nodiscard]] bool InjectKey(uint16_t scanCode,
                               uint16_t flags) noexcept override;

  void WaitForData(uint32_t timeoutMs) noexcept override;
  [[nodiscard]] bool GetStatus(BackendStatus &out) noexcept override;

  [[nodiscard]] const char *Name() const noexcept override {
    return "RawInputBackend";
  }

  [[nodiscard]] bool CanSuppressPhysical() const noexcept override {
    return false;
  }

private:
  struct StartupState {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool ok = false;
  };

  static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) noexcept;
  LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
  void ThreadMain() noexcept;
  void HandleRawInput(HRAWINPUT handle) noexcept;
  void PushEvent(const NEO_KEY_EVENT &evt) noexcept;
  [[nodiscard]] static int64_t QueryQpc() noexcept;

private:
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<DWORD> threadId_{0};

  HWND hwnd_ = nullptr;
  ATOM windowClass_ = 0;

  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::deque<NEO_KEY_EVENT> queue_{};

  std::atomic<uint32_t> sequenceCounter_{0};
  BackendStatus status_{};

  StartupState startup_{};
};
