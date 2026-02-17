#include "Utils.h"

#include <cstdarg>
#include <cstdio>

void Utils::LogInfo(const char *fmt, ...) noexcept {
  std::va_list args;
  va_start(args, fmt);
  std::fprintf(stdout, "[NeoStrafe] ");
  std::vfprintf(stdout, fmt, args);
  std::fprintf(stdout, "\n");
  va_end(args);
}

void Utils::LogError(const char *fmt, ...) noexcept {
  std::va_list args;
  va_start(args, fmt);
  std::fprintf(stderr, "[NeoStrafe] ERROR: ");
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  va_end(args);
}
