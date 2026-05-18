#include "Config.h"

namespace Config {
    std::atomic<bool> EnableJitter{false};
    std::atomic<int> JitterMs{0};
}
