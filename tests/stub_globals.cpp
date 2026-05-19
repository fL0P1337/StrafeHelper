#include "globals.h"
#include "Application.h"
#include <vector>
#include <mutex>

namespace Globals {

HWND g_hWindow = nullptr;
HINSTANCE g_hInstance = nullptr;

std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch = 0;
std::mutex g_activeKeysMutex;
std::atomic<bool> g_isCSpamActive = false;

SuperglideStats g_superglideStats{};

std::atomic<std::atomic<int>*> g_bindingTarget = nullptr;

NOTIFYICONDATA g_nid{};

} // namespace Globals

std::vector<std::pair<int, bool>> g_mockInjectedKeys;
std::mutex g_mockInjectedKeysMutex;

bool InjectKey(int vk, bool keyDown) noexcept {
    std::lock_guard<std::mutex> lock(g_mockInjectedKeysMutex);
    g_mockInjectedKeys.push_back({vk, keyDown});
    return true;
}
