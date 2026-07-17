#include "Globals.h"
#include "Application.h"
#include <vector>
#include <mutex>

namespace Globals {

HWND g_hWindow = nullptr;
HINSTANCE g_hInstance = nullptr;

// Fixed-size array matching the production definition.
KeyState g_KeyInfo[256];
std::atomic<uint32_t> g_lurchState{0};
std::atomic<bool> g_isSpamActive{false};

SuperglideStats g_superglideStats{};

std::atomic<std::atomic<int>*> g_bindingTarget = nullptr;


} // namespace Globals

std::vector<std::pair<int, bool>> g_mockInjectedKeys;
std::mutex g_mockInjectedKeysMutex;

bool InjectKey(int vk, bool keyDown) noexcept {
    std::lock_guard<std::mutex> lock(g_mockInjectedKeysMutex);
    g_mockInjectedKeys.push_back({vk, keyDown});
    return true;
}
