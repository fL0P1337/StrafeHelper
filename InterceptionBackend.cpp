#define WIN32_LEAN_AND_MEAN
#include "InterceptionBackend.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <type_traits>

namespace {
constexpr wchar_t kInterceptionDll[] = L"interception.dll";
constexpr wchar_t kInterceptionDll64[] = L"interception64.dll";
constexpr size_t kStrokeBatch = 32;

[[nodiscard]] long long QueryQpc() noexcept {
  LARGE_INTEGER qpc{};
  if (!QueryPerformanceCounter(&qpc)) {
    return 0;
  }
  return qpc.QuadPart;
}
} // namespace

InterceptionBackend::~InterceptionBackend() noexcept { Shutdown(); }

bool InterceptionBackend::Initialize() noexcept {
  if (initialized_) {
    return true;
  }

  if (!ResolveApi()) {
    LogError("[InterceptionBackend] failed to load Interception runtime or "
             "resolve required exports.");
    Shutdown();
    return false;
  }

  context_ = interceptionCreateContext_();
  if (!context_) {
    LogError("[InterceptionBackend] interception_create_context failed.");
    Shutdown();
    return false;
  }

  interceptionSetFilter_(
      context_, interceptionIsKeyboard_,
      static_cast<InterceptionFilter>(INTERCEPTION_FILTER_KEY_ALL));

  // Explicitly set mouse filter to none to ensure no mouse events are intercepted.
  interceptionSetFilter_(
      context_, interceptionIsMouse_,
      static_cast<InterceptionFilter>(INTERCEPTION_FILTER_MOUSE_NONE));

  if (!EnumerateKeyboards()) {
    LogError("[InterceptionBackend] failed to enumerate keyboard devices.");
    Shutdown();
    return false;
  }

  eventsCaptured_.store(0, std::memory_order_relaxed);
  eventsDropped_.store(0, std::memory_order_relaxed);
  eventsInjected_.store(0, std::memory_order_relaxed);

  healthy_.store(true, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this]() { ThreadMain(); });

  initialized_ = true;

  Logger::GetInstance().Log(
      "[InterceptionBackend] Initialized. Keyboards enumerated: " +
      std::to_string(keyboardDevices_.size()));
  return true;
}

void InterceptionBackend::Shutdown() noexcept {
  running_.store(false, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
  healthy_.store(false, std::memory_order_release);

  initialized_ = false;
  lastKeyboardDevice_ = 0;
  sequenceCounter_.store(0, std::memory_order_relaxed);
  keyboardDevices_.clear();
  callback_ = nullptr;

  // Counters retained on shutdown so any final reads from GetStatus stay
  // valid. They will be reset on the next Initialize() call.

  if (context_ && interceptionDestroyContext_) {
    interceptionDestroyContext_(context_);
  }
  context_ = nullptr;

  interceptionCreateContext_ = nullptr;
  interceptionDestroyContext_ = nullptr;
  interceptionSetFilter_ = nullptr;
  interceptionWait_ = nullptr;
  interceptionWaitWithTimeout_ = nullptr;
  interceptionReceive_ = nullptr;
  interceptionSend_ = nullptr;
  interceptionGetHardwareId_ = nullptr;
  interceptionIsInvalid_ = nullptr;
  interceptionIsKeyboard_ = nullptr;
  interceptionIsMouse_ = nullptr;

  if (interceptionLib_) {
    FreeLibrary(interceptionLib_);
    interceptionLib_ = nullptr;
  }
}

void InterceptionBackend::SetCallback(EventCallback cb) noexcept {
  callback_ = cb;
}

bool InterceptionBackend::InjectKey(uint16_t scanCode,
                                    uint16_t flags) noexcept {
  if (!initialized_) {
    return false;
  }

  // Use SendInput instead of interception_send to prevent synthetic events
  // from being re-captured by our own Interception filter (FILTER_KEY_ALL).
  // SendInput enters the Windows input pipeline downstream of the Interception
  // filter driver, so these events are never re-ingested.
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wScan = scanCode;
  input.ki.dwFlags = KEYEVENTF_SCANCODE;
  input.ki.dwExtraInfo = NEO_SYNTHETIC_INFORMATION;

  if ((flags & NEO_KEY_BREAK) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  if ((flags & NEO_KEY_E0) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }

  const UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    return false;
  }

  eventsInjected_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

bool InterceptionBackend::GetStatus(BackendStatus &out) noexcept {
  out.driverActive = initialized_ &&
                     running_.load(std::memory_order_acquire) &&
                     healthy_.load(std::memory_order_acquire);
  out.eventsCaptured = eventsCaptured_.load(std::memory_order_relaxed);
  out.eventsDropped = eventsDropped_.load(std::memory_order_relaxed);
  out.eventsInjected = eventsInjected_.load(std::memory_order_relaxed);
  return true;
}

void InterceptionBackend::ThreadMain() noexcept {
  constexpr uint32_t kPollTimeoutMs = 10;
  std::array<InterceptionStroke, kStrokeBatch> strokes{};

  while (running_.load(std::memory_order_relaxed)) {
    const InterceptionDevice device = WaitDevice(kPollTimeoutMs);
    if (device <= 0) {
      continue;
    }

    if (!interceptionIsKeyboard_(device)) {
      DrainNonKeyboardDevice(device);
      continue;
    }

    const int received = interceptionReceive_(context_, device, strokes.data(), kStrokeBatch);
    if (received <= 0) {
      MarkUnhealthy();
      break;
    }

    bool sendFailed = false;
    for (int i = 0; i < received; ++i) {
      const auto *keyStroke = reinterpret_cast<const InterceptionKeyStroke *>(&strokes[static_cast<size_t>(i)]);

      uint16_t neoFlags = NEO_KEY_MAKE;
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_UP)) != 0u) {
        neoFlags |= NEO_KEY_BREAK;
      }
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_E0)) != 0u) {
        neoFlags |= NEO_KEY_E0;
      }
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_E1)) != 0u) {
        neoFlags |= NEO_KEY_E1;
      }

      NEO_KEY_EVENT evt{};
      evt.scanCode = keyStroke->code;
      evt.flags = neoFlags;
      evt.sequence = sequenceCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
      evt.timestampQpc = QueryQpc();
      evt.sourceDevice = device;
      evt.nativeState = keyStroke->state;
      evt.nativeInformation = keyStroke->information;

      eventsCaptured_.fetch_add(1, std::memory_order_relaxed);

      bool suppress = false;
      if (callback_) {
        suppress = callback_(evt);
      }

      if (suppress) {
        eventsDropped_.fetch_add(1, std::memory_order_relaxed);
      } else {
        if (!SendOnDevice(device, *keyStroke, false)) {
          sendFailed = true;
          break;
        }
      }
    }

    if (sendFailed) {
      MarkUnhealthy();
      break;
    }
  }
}

void InterceptionBackend::MarkUnhealthy() noexcept {
  if (!healthy_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  running_.store(false, std::memory_order_release);
  if (context_ && interceptionSetFilter_ && interceptionIsKeyboard_) {
    interceptionSetFilter_(
        context_, interceptionIsKeyboard_,
        static_cast<InterceptionFilter>(INTERCEPTION_FILTER_KEY_NONE));
  }
  if (Globals::g_hWindow) {
    PostMessageW(Globals::g_hWindow, Globals::WM_BACKEND_FAILED, 0, 0);
  }
}

bool InterceptionBackend::ResolveApi() noexcept {
  std::wstring exeDir = GetExecutableDirectory();
  if (exeDir.empty()) {
    return false;
  }

  auto tryLoad = [this, &exeDir](const wchar_t *dllName) -> bool {
    std::wstring fullPath = exeDir + dllName;
    interceptionLib_ = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!interceptionLib_) {
      return false;
    }
    Logger::GetInstance().Log("[InterceptionBackend] Loaded interception DLL.");
    return true;
  };

  if (!interceptionLib_ && !tryLoad(kInterceptionDll) &&
      !tryLoad(kInterceptionDll64)) {
    return false;
  }

  auto resolve = [this](auto &target, const char *name) -> bool {
    target = reinterpret_cast<std::decay_t<decltype(target)>>(
        GetProcAddress(interceptionLib_, name));
    if (!target) {
      LogError(std::string("[InterceptionBackend] missing export: ") + name,
               GetLastError());
      return false;
    }
    return true;
  };

  return resolve(interceptionCreateContext_, "interception_create_context") &&
         resolve(interceptionDestroyContext_, "interception_destroy_context") &&
         resolve(interceptionSetFilter_, "interception_set_filter") &&
         resolve(interceptionWait_, "interception_wait") &&
         resolve(interceptionWaitWithTimeout_,
                 "interception_wait_with_timeout") &&
         resolve(interceptionSend_, "interception_send") &&
         resolve(interceptionReceive_, "interception_receive") &&
         resolve(interceptionGetHardwareId_, "interception_get_hardware_id") &&
         resolve(interceptionIsInvalid_, "interception_is_invalid") &&
         resolve(interceptionIsKeyboard_, "interception_is_keyboard") &&
         resolve(interceptionIsMouse_, "interception_is_mouse");
}

bool InterceptionBackend::EnumerateKeyboards() noexcept {
  keyboardDevices_.clear();

  for (InterceptionDevice device = 1; device <= INTERCEPTION_MAX_DEVICE;
       ++device) {
    if (!interceptionIsKeyboard_(device)) {
      continue;
    }

    std::array<char, 512> hardwareId{};
    const unsigned int hwidLen = interceptionGetHardwareId_(
        context_, device, hardwareId.data(),
        static_cast<unsigned int>(hardwareId.size()));
    if (hwidLen > 0) {
      keyboardDevices_.push_back(device);
    }
  }

  if (keyboardDevices_.empty()) {
    for (InterceptionDevice idx = 0; idx < INTERCEPTION_MAX_KEYBOARD; ++idx) {
      keyboardDevices_.push_back(INTERCEPTION_KEYBOARD(idx));
    }
  }

  if (keyboardDevices_.empty()) {
    return false;
  }

  lastKeyboardDevice_ = keyboardDevices_.front();
  return true;
}

InterceptionDevice
InterceptionBackend::WaitDevice(uint32_t timeoutMs) noexcept {
  if (!context_) {
    return 0;
  }

  const InterceptionDevice device =
      (timeoutMs == INFINITE)
          ? interceptionWait_(context_)
          : interceptionWaitWithTimeout_(context_, timeoutMs);

  if (device <= 0 || interceptionIsInvalid_(device)) {
    return 0;
  }
  return device;
}

void InterceptionBackend::DrainNonKeyboardDevice(
    InterceptionDevice device) noexcept {
  if (!context_) {
    return;
  }

  // If we somehow receive mouse events, pass them through immediately
  // instead of discarding them. This prevents mouse freezes/lag if
  // the filter logic fails or driver behavior is unexpected.
  std::array<InterceptionStroke, 8> strokes{};
  while (true) {
    const int received = interceptionReceive_(
        context_, device, strokes.data(),
        static_cast<unsigned int>(strokes.size()));
    if (received <= 0) {
      break;
    }
    interceptionSend_(context_, device, strokes.data(), received);
  }
}

bool InterceptionBackend::SendOnDevice(InterceptionDevice device,
                                       const InterceptionKeyStroke &stroke,
                                       bool countInjected) noexcept {
  if (!context_ || device <= 0) {
    return false;
  }
  if (interceptionIsInvalid_(device) || !interceptionIsKeyboard_(device)) {
    return false;
  }

  const auto *rawStroke = reinterpret_cast<const InterceptionStroke *>(&stroke);
  const int sent = interceptionSend_(context_, device, rawStroke, 1);
  if (sent <= 0) {
    return false;
  }

  if (countInjected) {
    eventsInjected_.fetch_add(1, std::memory_order_relaxed);
  }
  return true;
}
