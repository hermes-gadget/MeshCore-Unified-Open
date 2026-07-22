#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using byte = uint8_t;

inline unsigned long millis() { return 0; }

inline char* ltoa(long value, char* output, int radix) {
  if (!output || radix != 10) return output;
  std::snprintf(output, 32, "%ld", value);
  return output;
}
