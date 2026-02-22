#define WIN32_LEAN_AND_MEAN
#include "InterceptionBackend.h"
#include "Logger.h"
#include "Utils.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <type_traits>

namespace {
constexpr wchar_t kInterceptionDll[] = L"interception.dll";
constexpr wchar_t kInterceptionDll64[] = L"interception64.dll";
constexpr size_t kStrokeBatch = 32;

[[nodiscard]] long ClampAddLong(long base, long delta) noexcept {
  if (delta <= 0) {
    return base;
  }
  if (base >= LONG_MAX - delta) {
    return LONG_MAX;
  }
  return base + delta;
}

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

  if (!EnumerateKeyboards()) {
    LogError("[InterceptionBackend] failed to enumerate keyboard devices.");
    Shutdown();
    return false;
  }

  status_ = {};
  status_.driverActive = true;
  initialized_ = true;

  Logger::GetInstance().Log(
      "[InterceptionBackend] Initialized. Keyboards enumerated: " +
      std::to_string(keyboardDevices_.size()));
  return true;
}

void InterceptionBackend::Shutdown() noexcept {
  initialized_ = false;
  pendingDevice_ = 0;
  lastKeyboardDevice_ = 0;
  sequenceCounter_ = 0;
  keyboardDevices_.clear();
  status_ = {};

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

  if (interceptionLib_) {
    FreeLibrary(interceptionLib_);
    interceptionLib_ = nullptr;
  }
}

uint32_t InterceptionBackend::PollEvents(NEO_KEY_EVENT *out,
                                         uint32_t maxCount) noexcept {
  if (!initialized_ || !out || maxCount == 0) {
    return 0;
  }

  uint32_t totalRead = 0;

  if (pendingDevice_ > 0) {
    totalRead += DrainKeyboardDevice(pendingDevice_, out + totalRead,
                                     maxCount - totalRead);
    pendingDevice_ = 0;
  }

  while (totalRead < maxCount) {
    const InterceptionDevice device = WaitDevice(0);
    if (device <= 0) {
      break;
    }

    if (!interceptionIsKeyboard_(device)) {
      DrainNonKeyboardDevice(device);
      continue;
    }

    const uint32_t readNow =
        DrainKeyboardDevice(device, out + totalRead, maxCount - totalRead);
    if (readNow == 0) {
      break;
    }
    totalRead += readNow;
  }

  return totalRead;
}

bool InterceptionBackend::PassThrough(const NEO_KEY_EVENT &event) noexcept {
  if (!initialized_ || !context_) {
    return false;
  }
  if (event.sourceDevice <= 0) {
    return false;
  }

  InterceptionKeyStroke stroke{};
  stroke.code = event.scanCode;
  stroke.state = event.nativeState;
  stroke.information = event.nativeInformation;
  return SendOnDevice(event.sourceDevice, stroke, false);
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

  if ((flags & NEO_KEY_BREAK) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  if ((flags & NEO_KEY_E0) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  // Note: E1 prefix (e.g. Pause/Break) is not supported by SendInput's
  // KEYEVENTF flags. E1 keys are never used for WASD movement.

  const UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    return false;
  }

  status_.eventsInjected = ClampAddLong(status_.eventsInjected, 1);
  return true;
}

void InterceptionBackend::WaitForData(uint32_t timeoutMs) noexcept {
  if (!initialized_ || pendingDevice_ > 0) {
    return;
  }

  const InterceptionDevice device = WaitDevice(timeoutMs);
  if (device <= 0) {
    return;
  }

  if (!interceptionIsKeyboard_(device)) {
    DrainNonKeyboardDevice(device);
    return;
  }

  pendingDevice_ = device;
  lastKeyboardDevice_ = device;
}

bool InterceptionBackend::GetStatus(BackendStatus &out) noexcept {
  if (!initialized_) {
    return false;
  }

  status_.driverActive = context_ != nullptr;
  out = status_;
  return true;
}

bool InterceptionBackend::ResolveApi() noexcept {
  auto tryLoad = [this](const wchar_t *dllName) -> bool {
    interceptionLib_ = LoadLibraryW(dllName);
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
         resolve(interceptionIsKeyboard_, "interception_is_keyboard");
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

uint32_t InterceptionBackend::DrainKeyboardDevice(InterceptionDevice device,
                                                  NEO_KEY_EVENT *out,
                                                  uint32_t maxCount) noexcept {
  if (!context_ || !out || maxCount == 0) {
    return 0;
  }

  uint32_t produced = 0;
  std::array<InterceptionStroke, kStrokeBatch> strokes{};
  while (produced < maxCount) {
    const unsigned int request = static_cast<unsigned int>(std::min<uint32_t>(
        maxCount - produced, static_cast<uint32_t>(kStrokeBatch)));
    const int received =
        interceptionReceive_(context_, device, strokes.data(), request);
    if (received <= 0) {
      break;
    }

    for (int i = 0; i < received && produced < maxCount; ++i) {
      const auto *keyStroke = reinterpret_cast<const InterceptionKeyStroke *>(
          &strokes[static_cast<size_t>(i)]);

      uint16_t neoFlags = NEO_KEY_MAKE;
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_UP)) !=
          0u) {
        neoFlags |= NEO_KEY_BREAK;
      }
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_E0)) !=
          0u) {
        neoFlags |= NEO_KEY_E0;
      }
      if ((keyStroke->state & static_cast<uint16_t>(INTERCEPTION_KEY_E1)) !=
          0u) {
        neoFlags |= NEO_KEY_E1;
      }

      out[produced].scanCode = keyStroke->code;
      out[produced].flags = neoFlags;
      out[produced].sequence = ++sequenceCounter_;
      out[produced].timestampQpc = QueryQpc();
      out[produced].sourceDevice = device;
      out[produced].nativeState = keyStroke->state;
      out[produced].nativeInformation = keyStroke->information;
      ++produced;
    }

    if (received < static_cast<int>(request)) {
      break;
    }
  }

  lastKeyboardDevice_ = device;
  status_.eventsCaptured =
      ClampAddLong(status_.eventsCaptured, static_cast<long>(produced));
  return produced;
}

void InterceptionBackend::DrainNonKeyboardDevice(
    InterceptionDevice device) noexcept {
  if (!context_) {
    return;
  }

  std::array<InterceptionStroke, 8> discard{};
  while (interceptionReceive_(context_, device, discard.data(),
                              static_cast<unsigned int>(discard.size())) > 0) {
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
    status_.eventsInjected = ClampAddLong(status_.eventsInjected, 1);
  }
  return true;
}
