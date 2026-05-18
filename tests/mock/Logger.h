#pragma once

#include <string>

class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }
    void Log(const std::string& msg);
};
