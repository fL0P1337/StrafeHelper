#include "Utils.h"
<<<<<<< HEAD
std::string FormatVirtualKeyName(int vk) { return ""; }
=======
#include <string>

std::string FormatVirtualKeyName(int vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    return std::to_string(vk); // Mock implementation
}
>>>>>>> origin/jules-10012660180567200711-714cdd0a
