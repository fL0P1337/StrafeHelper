#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>

#include "../imgui/imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "../imgui/imgui_internal.h"

namespace elements {
bool tab(const char *name, bool boolean);
}
