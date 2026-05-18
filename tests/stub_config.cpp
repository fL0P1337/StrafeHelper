#include "Config.h"

std::atomic<bool> g_stopSuperglideRequest(false);

namespace Config {
    std::atomic<bool> EnableSuperglide(true);
    std::atomic<int> SuperglideBind(0);
    std::atomic<double> TargetFPS(60.0);
    void LoadConfig() {}
    void SaveConfig() {}
    std::string GetConfigPath() { return ""; }
}
