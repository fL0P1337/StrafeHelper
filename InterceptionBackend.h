#pragma once
/*
 * InterceptionBackend.h
 * Primary backend wrapper over Interception keyboard filter driver API.
 */

#include "InputBackend.h"

#include <vector>
#include <thread>
#include <atomic>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "interception.h"

class InterceptionBackend final : public InputBackend {
public:
  InterceptionBackend() noexcept = default;
  ~InterceptionBackend() noexcept override;

  [[nodiscard]] bool Initialize() noexcept override;
  void Shutdown() noexcept override;
  void SetCallback(EventCallback cb) noexcept override;

  [[nodiscard]] bool InjectKey(uint16_t scanCode,
                               uint16_t flags) noexcept override;

  [[nodiscard]] bool GetStatus(BackendStatus &out) noexcept override;

  [[nodiscard]] const char *Name() const noexcept override {
    return "InterceptionBackend";
  }

  [[nodiscard]] bool CanSuppressPhysical() const noexcept override {
    return true;
  }

private:
  using create_context_fn = decltype(&interception_create_context);
  using destroy_context_fn = decltype(&interception_destroy_context);
  using set_filter_fn = decltype(&interception_set_filter);
  using wait_fn = decltype(&interception_wait);
  using wait_timeout_fn = decltype(&interception_wait_with_timeout);
  using receive_fn = decltype(&interception_receive);
  using send_fn = decltype(&interception_send);
  using hardware_id_fn = decltype(&interception_get_hardware_id);
  using is_invalid_fn = decltype(&interception_is_invalid);
  using is_keyboard_fn = decltype(&interception_is_keyboard);
  using is_mouse_fn = decltype(&interception_is_mouse);

  [[nodiscard]] bool ResolveApi() noexcept;
  [[nodiscard]] bool EnumerateKeyboards() noexcept;
  [[nodiscard]] InterceptionDevice WaitDevice(uint32_t timeoutMs) noexcept;
  void DrainNonKeyboardDevice(InterceptionDevice device) noexcept;
  [[nodiscard]] bool SendOnDevice(InterceptionDevice device,
                                  const InterceptionKeyStroke &stroke,
                                  bool countInjected) noexcept;
  void ThreadMain() noexcept;

private:
  HMODULE interceptionLib_ = nullptr;

  create_context_fn interceptionCreateContext_ = nullptr;
  destroy_context_fn interceptionDestroyContext_ = nullptr;
  set_filter_fn interceptionSetFilter_ = nullptr;
  wait_fn interceptionWait_ = nullptr;
  wait_timeout_fn interceptionWaitWithTimeout_ = nullptr;
  receive_fn interceptionReceive_ = nullptr;
  send_fn interceptionSend_ = nullptr;
  hardware_id_fn interceptionGetHardwareId_ = nullptr;
  is_invalid_fn interceptionIsInvalid_ = nullptr;
  is_keyboard_fn interceptionIsKeyboard_ = nullptr;
  is_mouse_fn interceptionIsMouse_ = nullptr;

  InterceptionContext context_ = nullptr;
  std::vector<InterceptionDevice> keyboardDevices_{};
  InterceptionDevice lastKeyboardDevice_ = 0;
  
  std::thread thread_;
  std::atomic<bool> running_{false};
  EventCallback callback_ = nullptr;

  // Atomic counters so neither the polling thread nor InjectKey ever takes
  // a mutex on the input hot path.
  std::atomic<uint32_t> sequenceCounter_{0};
  std::atomic<long> eventsCaptured_{0};
  std::atomic<long> eventsDropped_{0};
  std::atomic<long> eventsInjected_{0};
  bool initialized_ = false;
};
