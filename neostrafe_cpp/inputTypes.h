#pragma once
#include <windows.h>

namespace inputs {
    class IInputManager {
    public:
        virtual ~IInputManager() = default;
        virtual bool sendGameInput(int key, bool isKeyDown) = 0;
        virtual bool initDirectInput(HWND hwnd) = 0;
        virtual bool setupKeyboardHook() = 0;
        virtual bool initializeEvents() = 0;
        virtual void cleanup() = 0;
    };
}