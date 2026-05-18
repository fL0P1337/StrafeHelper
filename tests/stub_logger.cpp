#include <string>
#include <iostream>

namespace Logger {
    void Log(const std::string& message) {
        std::cout << "Mock Log: " << message << std::endl;
    }
}
