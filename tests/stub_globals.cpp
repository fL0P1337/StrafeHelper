#include "globals.h"

namespace Globals {

HHOOK g_hHook = nullptr;
HWND g_hWindow = nullptr;
HANDLE g_hHookThread = nullptr;
HANDLE g_hSpamThread = nullptr;
HANDLE g_hSpamEvent = nullptr;
HANDLE g_hTurboLootThread = nullptr;
HANDLE g_hTurboLootEvent = nullptr;
HANDLE g_hTurboJumpThread = nullptr;
HANDLE g_hTurboJumpEvent = nullptr;
HANDLE g_hSuperglideThread = nullptr;
HANDLE g_hSuperglideEvent = nullptr;
HINSTANCE g_hInstance = nullptr;

std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch = 0;
CRITICAL_SECTION g_csActiveKeys;
std::atomic<bool> g_isCSpamActive = false;

SuperglideStats g_superglideStats;

std::atomic<std::atomic<int>*> g_bindingTarget = nullptr;

NOTIFYICONDATA g_nid;

} // namespace Globals
