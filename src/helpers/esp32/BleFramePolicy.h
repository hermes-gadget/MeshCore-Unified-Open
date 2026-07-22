#pragma once

#include <cstddef>
#include <cstdint>

namespace meshcore {
namespace ble {

static constexpr uint16_t ATT_HEADER_SIZE = 3;
static constexpr uint16_t DEFAULT_ATT_MTU = 23;
static constexpr uint16_t PREFERRED_ATT_MTU = 517;

inline size_t payloadCapacity(uint16_t mtu) {
  return mtu > ATT_HEADER_SIZE ? static_cast<size_t>(mtu - ATT_HEADER_SIZE) : 0;
}

inline bool frameFitsMtu(size_t frame_len, uint16_t mtu) {
  return frame_len > 0 && frame_len <= payloadCapacity(mtu);
}

} // namespace ble
} // namespace meshcore
