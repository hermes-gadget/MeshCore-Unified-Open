#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace mesh {

Packet::Packet() {
  header = 0;
  path_len = 0;
  payload_len = 0;
}

bool Packet::isValidPathLen(uint8_t path_len) {
  uint8_t hash_count = path_len & 63;
  uint8_t hash_size = (path_len >> 6) + 1;
  if (hash_size == 4) return false;  // Reserved for future
  return hash_count*hash_size <= MAX_PATH_SIZE;
}

size_t Packet::writePath(uint8_t* dest, const uint8_t* src, uint8_t path_len) {
  uint8_t hash_count = path_len & 63;
  uint8_t hash_size = (path_len >> 6) + 1;
  size_t len = hash_count*hash_size;
  if (len > MAX_PATH_SIZE) {
    MESH_DEBUG_PRINTLN("Packet::copyPath, invalid path_len=%d", (uint32_t)path_len);
    return 0;   // Error
  }
  memcpy(dest, src, len);
  return len;
}

uint8_t Packet::copyPath(uint8_t* dest, const uint8_t* src, uint8_t path_len) {
  writePath(dest, src, path_len);
  return path_len;
}

int Packet::getRawLength() const {
  return 2 + getPathByteLen() + payload_len + (hasTransportCodes() ? 4 : 0);
}

void Packet::calculatePacketHash(uint8_t* hash) const {
  SHA256 sha;
  uint8_t t = getPayloadType();
  sha.update(&t, 1);
  if (t == PAYLOAD_TYPE_TRACE) {
    sha.update(&path_len, sizeof(path_len));   // CAVEAT: TRACE packets can revisit same node on return path
  }
  sha.update(payload, payload_len);
  sha.finalize(hash, MAX_HASH_SIZE);
}

uint8_t Packet::writeTo(uint8_t dest[]) const {
  uint8_t i = 0;
  dest[i++] = header;
  if (hasTransportCodes()) {
    memcpy(&dest[i], &transport_codes[0], 2); i += 2;
    memcpy(&dest[i], &transport_codes[1], 2); i += 2;
  }
  dest[i++] = path_len;
  i += writePath(&dest[i], path, path_len);
  memcpy(&dest[i], payload, payload_len); i += payload_len;
  return i;
}

bool Packet::readFrom(const uint8_t src[], uint8_t len) {
  if (!src || len == 0) return false;
  uint8_t i = 0;
  header = src[i++];
  if (hasTransportCodes()) {
    if (i + 4 > len) return false;
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
  } else {
    transport_codes[0] = transport_codes[1] = 0;
  }
  if (i >= len) return false;
  path_len = src[i++];
  if (!isValidPathLen(path_len)) return false;   // bad encoding

  uint8_t bl = getPathByteLen();
  if (static_cast<size_t>(i) + bl > len) return false;
  memcpy(path, &src[i], bl); i += bl;

  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // bad encoding
  if (payload_len < minimumPayloadLength(getPayloadType())) return false;
  if (payload_len > 0) {
    memcpy(payload, &src[i], payload_len);
  }
  return true;   // success
}

size_t Packet::minimumPayloadLength(uint8_t payload_type) {
  // Floor sizes for the fixed outer shapes used by MeshCore v1 payloads.
  // Encrypted blobs still require their MAC/hash prefixes even when empty.
  switch (payload_type) {
    case PAYLOAD_TYPE_REQ:       // dest + src + MAC
    case PAYLOAD_TYPE_RESPONSE:  // dest + src + MAC
    case PAYLOAD_TYPE_TXT_MSG:   // dest + src + MAC (+ timestamp inside cipher)
    case PAYLOAD_TYPE_PATH:      // dest + src + MAC
      return 4;
    case PAYLOAD_TYPE_ACK:       // truncated hash
      return 4;
    case PAYLOAD_TYPE_ADVERT:    // identity advert header floor
      return 32;
    case PAYLOAD_TYPE_GRP_TXT:   // channel hash + MAC
    case PAYLOAD_TYPE_GRP_DATA:  // channel hash + MAC
      return 3;
    case PAYLOAD_TYPE_ANON_REQ:  // dest + ephemeral pub key + MAC
      return 1 + PUB_KEY_SIZE + 2;
    case PAYLOAD_TYPE_TRACE:     // tag + auth + path
      return 9;
    case PAYLOAD_TYPE_MULTIPART: // multipart header
      return 3;
    case PAYLOAD_TYPE_CONTROL:   // control opcode
      return 1;
    default:
      return 0;
  }
}

}