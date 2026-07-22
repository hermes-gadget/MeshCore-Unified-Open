#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mock AES128 class for testing
// Provides minimal interface to allow Utils.cpp to compile
class AES128 {
public:
  void setKey(const uint8_t* key, size_t keySize) {}
  void encryptBlock(uint8_t* output, const uint8_t* input) { memcpy(output, input, 16); }
  void decryptBlock(uint8_t* output, const uint8_t* input) { memcpy(output, input, 16); }
};
