#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

inline void ed25519_create_keypair(uint8_t* public_key, uint8_t* private_key,
                                   const uint8_t* seed) {
  if (public_key) std::memcpy(public_key, seed, 32);
  if (private_key) {
    std::memcpy(private_key, seed, 32);
    std::memcpy(private_key + 32, seed, 32);
  }
}

inline void ed25519_derive_pub(uint8_t* public_key, const uint8_t* private_key) {
  if (public_key) std::memcpy(public_key, private_key, 32);
}

inline void ed25519_sign(uint8_t* signature, const uint8_t*, size_t,
                         const uint8_t*, const uint8_t*) {
  if (signature) std::memset(signature, 0, 64);
}

inline void ed25519_key_exchange(uint8_t* shared_secret, const uint8_t* public_key,
                                 const uint8_t*) {
  if (shared_secret) std::memcpy(shared_secret, public_key, 32);
}
