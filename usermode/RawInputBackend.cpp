#define WIN32_LEAN_AND_MEAN
#include "RawInputBackend.h"

#include <chrono>
#include <vector>

namespace {
constexpr wchar_t kRawInputWindowClass[] = L"NeoStrafeRawInputWindow";
}

RawInputBackend::~RawInputBackend() noexcept { Shutdown(); }

bool RawInputBackend::Initialize() noexcept {
  if (running_.load(std::memory_order_relaxed)) {
    return true;
  }

  {
    std::lock_guard<std::mutex> lock(startup_.mutex);
    startup_.done = false;
    startup_.ok = false;
  }

  running_.store(true, std::memory_order_relaxed);
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

void RawInputBackend::Shutdown() noexcept {
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
}

uint32_t RawInputBackend::PollEvents(NEO_KEY_EVENT *out,
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

bool RawInputBackend::PassThrough(const NEO_KEY_EVENT &) noexcept {
  return true;
}

bool RawInputBackend::InjectKey(uint16_t scanCode, uint16_t flags) noexcept {
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

void RawInputBackend::WaitForData(uint32_t timeoutMs) noexcept {
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

bool RawInputBackend::GetStatus(BackendStatus &out) noexcept {
  std::lock_guard<std::mutex> lock(queueMutex_);
  out = status_;
  out.driverActive = running_.load(std::memory_order_relaxed);
  return true;
}

LRESULT CALLBACK RawInputBackend::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam,
                                                LPARAM lParam) noexcept {
  RawInputBackend *self =
      reinterpret_cast<RawInputBackend *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    const auto *cs = reinterpret_cast<const CREATESTRUCTW *>(lParam);
    self = static_cast<RawInputBackend *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (!self) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  return self->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT RawInputBackend::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam) noexcept {
  switch (msg) {
  case WM_INPUT:
    HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
    return 0;
  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RawInputBackend::ThreadMain() noexcept {
  threadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProcStatic;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kRawInputWindowClass;
  windowClass_ = RegisterClassExW(&wc);

  if (windowClass_ == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    std::lock_guard<std::mutex> lock(startup_.mutex);
    startup_.ok = false;
    startup_.done = true;
    startup_.cv.notify_one();
    threadId_.store(0, std::memory_order_relaxed);
    return;
  }

  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kRawInputWindowClass,
                          L"NeoStrafeRawInput", WS_OVERLAPPED, 0, 0, 0, 0,
                          nullptr, nullptr, GetModuleHandleW(nullptr), this);

  bool ok = false;
  if (hwnd_) {
    ShowWindow(hwnd_, SW_HIDE);

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x06;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = hwnd_;
    ok = RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
  }

  {
    std::lock_guard<std::mutex> lock(startup_.mutex);
    startup_.ok = ok;
    startup_.done = true;
  }
  startup_.cv.notify_one();

  if (!ok) {
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
    if (windowClass_ != 0) {
      UnregisterClassW(kRawInputWindowClass, GetModuleHandleW(nullptr));
      windowClass_ = 0;
    }
    threadId_.store(0, std::memory_order_relaxed);
    return;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (windowClass_ != 0) {
    UnregisterClassW(kRawInputWindowClass, GetModuleHandleW(nullptr));
    windowClass_ = 0;
  }

  threadId_.store(0, std::memory_order_relaxed);
}

void RawInputBackend::HandleRawInput(HRAWINPUT handle) noexcept {
  UINT size = 0;
  if (GetRawInputData(handle, RID_INPUT, nullptr, &size,
                      sizeof(RAWINPUTHEADER)) != 0 ||
      size == 0) {
    return;
  }

  std::vector<unsigned char> buffer(size);
  if (GetRawInputData(handle, RID_INPUT, buffer.data(), &size,
                      sizeof(RAWINPUTHEADER)) != size) {
    return;
  }

  const auto *raw = reinterpret_cast<const RAWINPUT *>(buffer.data());
  if (!raw || raw->header.dwType != RIM_TYPEKEYBOARD) {
    return;
  }

  const RAWKEYBOARD &kbd = raw->data.keyboard;

  if (raw->header.hDevice == nullptr) {
    return;
  }

  uint16_t scan = static_cast<uint16_t>(kbd.MakeCode);
  if (scan == 0) {
    scan = static_cast<uint16_t>(
        MapVirtualKeyW(static_cast<UINT>(kbd.VKey), MAPVK_VK_TO_VSC));
  }
  if (scan == 0) {
    return;
  }

  NEO_KEY_EVENT evt{};
  evt.scanCode = scan;
  evt.flags = NEO_KEY_MAKE;
  if ((kbd.Flags & RI_KEY_BREAK) != 0u) {
    evt.flags |= NEO_KEY_BREAK;
  }
  if ((kbd.Flags & RI_KEY_E0) != 0u) {
    evt.flags |= NEO_KEY_E0;
  }
  if ((kbd.Flags & RI_KEY_E1) != 0u) {
    evt.flags |= NEO_KEY_E1;
  }

  evt.sequence = ++sequenceCounter_;
  evt.timestampQpc = QueryQpc();
  evt.sourceDevice = 1;
  evt.nativeState = static_cast<uint16_t>(kbd.Flags);
  evt.nativeInformation = static_cast<uint32_t>(kbd.ExtraInformation);

  if (evt.nativeInformation == NEO_SYNTHETIC_INFORMATION) {
    return;
  }

  PushEvent(evt);
}

void RawInputBackend::PushEvent(const NEO_KEY_EVENT &evt) noexcept {
  std::lock_guard<std::mutex> lock(queueMutex_);
  queue_.push_back(evt);
  ++status_.eventsCaptured;
  queueCv_.notify_one();
}

int64_t RawInputBackend::QueryQpc() noexcept {
  LARGE_INTEGER qpc{};
  if (!QueryPerformanceCounter(&qpc)) {
    return 0;
  }
  return qpc.QuadPart;
}
