#define WIN32_LEAN_AND_MEAN
#include "KbdHookBackend.h"

#include <algorithm>
#include <chrono>

KbdHookBackend *KbdHookBackend::instance_ = nullptr;

KbdHookBackend::~KbdHookBackend() noexcept { Shutdown(); }

bool KbdHookBackend::Initialize() noexcept {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(startup_.mutex);
    startup_.done = false;
    startup_.ok = false;
  }

  running_.store(true, std::memory_order_relaxed);
  instance_ = this;

  thread_ = std::thread([this]() { ThreadMain(); });

  std::unique_lock<std::mutex> lock(startup_.mutex);
  startup_.cv.wait(lock, [&]() { return startup_.done; });
  if (!startup_.ok) {
    running_.store(false, std::memory_order_relaxed);
    lock.unlock();
    Shutdown();
    return false;
  }

  {
    std::lock_guard<std::mutex> qLock(queueMutex_);
    status_ = {};
    status_.driverActive = true;
  }

  return true;
}

void KbdHookBackend::Shutdown() noexcept {
  running_.store(false, std::memory_order_relaxed);

  const DWORD tid = threadId_.load(std::memory_order_relaxed);
  if (tid != 0) {
    (void)PostThreadMessageW(tid, WM_QUIT, 0, 0);
  }
  queueCv_.notify_all();

  if (thread_.joinable()) {
    thread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
    status_.driverActive = false;
  }

  threadId_.store(0, std::memory_order_relaxed);
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

uint32_t KbdHookBackend::PollEvents(NEO_KEY_EVENT *out,
                                    uint32_t maxCount) noexcept {
  if (!out || maxCount == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(queueMutex_);
  uint32_t count = 0;
  while (!queue_.empty() && count < maxCount) {
    out[count++] = queue_.front();
    queue_.pop_front();
  }
  return count;
}

bool KbdHookBackend::PassThrough(const NEO_KEY_EVENT &) noexcept {
  return true;
}

bool KbdHookBackend::InjectKey(uint16_t scanCode, uint16_t flags) noexcept {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = 0;
  input.ki.wScan = scanCode;
  input.ki.dwFlags = KEYEVENTF_SCANCODE;
  if ((flags & NEO_KEY_BREAK) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  if ((flags & NEO_KEY_E0) != 0u) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  input.ki.dwExtraInfo = NEO_SYNTHETIC_INFORMATION;

  const UINT sent = SendInput(1, &input, sizeof(INPUT));
  if (sent != 1) {
    return false;
  }

  std::lock_guard<std::mutex> lock(queueMutex_);
  ++status_.eventsInjected;
  return true;
}

void KbdHookBackend::WaitForData(uint32_t timeoutMs) noexcept {
  std::unique_lock<std::mutex> lock(queueMutex_);
  if (!queue_.empty()) {
    return;
  }

  if (timeoutMs == INFINITE) {
    queueCv_.wait(lock, [&]() { return !queue_.empty() || !running_.load(); });
    return;
  }

  (void)queueCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
    return !queue_.empty() || !running_.load();
  });
}

bool KbdHookBackend::GetStatus(BackendStatus &out) noexcept {
  std::lock_guard<std::mutex> lock(queueMutex_);
  out = status_;
  out.driverActive = running_.load(std::memory_order_relaxed);
  return true;
}

LRESULT CALLBACK KbdHookBackend::HookProc(int nCode, WPARAM wParam,
                                          LPARAM lParam) noexcept {
  if (nCode != HC_ACTION || !instance_ ||
      !instance_->running_.load(std::memory_order_relaxed)) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN && wParam != WM_KEYUP &&
      wParam != WM_SYSKEYUP) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  const auto *kbd = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
  if (!kbd) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  if ((kbd->flags & LLKHF_INJECTED) != 0u &&
      static_cast<uint32_t>(kbd->dwExtraInfo) == NEO_SYNTHETIC_INFORMATION) {
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
  }

  NEO_KEY_EVENT evt{};
  evt.scanCode = static_cast<uint16_t>(kbd->scanCode);
  if (evt.scanCode == 0) {
    evt.scanCode = static_cast<uint16_t>(
        MapVirtualKeyW(static_cast<UINT>(kbd->vkCode), MAPVK_VK_TO_VSC));
  }

  evt.flags = NEO_KEY_MAKE;
  if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    evt.flags |= NEO_KEY_BREAK;
  }
  if ((kbd->flags & LLKHF_EXTENDED) != 0u) {
    evt.flags |= NEO_KEY_E0;
  }

  evt.sequence = ++instance_->sequenceCounter_;
  evt.timestampQpc = QueryQpc();
  evt.sourceDevice = 1;
  evt.nativeState = 0;
  evt.nativeInformation = static_cast<uint32_t>(kbd->dwExtraInfo);

  instance_->PushEvent(evt);
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void KbdHookBackend::ThreadMain() noexcept {
  threadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);
  hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);

  {
    std::lock_guard<std::mutex> lock(startup_.mutex);
    startup_.ok = (hook_ != nullptr);
    startup_.done = true;
  }
  startup_.cv.notify_one();

  if (!hook_) {
    threadId_.store(0, std::memory_order_relaxed);
    return;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  UnhookWindowsHookEx(hook_);
  hook_ = nullptr;
  threadId_.store(0, std::memory_order_relaxed);
}

void KbdHookBackend::PushEvent(const NEO_KEY_EVENT &evt) noexcept {
  std::lock_guard<std::mutex> lock(queueMutex_);
  queue_.push_back(evt);
  ++status_.eventsCaptured;
  queueCv_.notify_one();
}

int64_t KbdHookBackend::QueryQpc() noexcept {
  LARGE_INTEGER qpc{};
  if (!QueryPerformanceCounter(&qpc)) {
    return 0;
  }
  return qpc.QuadPart;
}
