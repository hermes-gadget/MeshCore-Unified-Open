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

size_t Packet::minimumPayloadLength(uint8_t payload_type) {
  switch (payload_type) {
    case PAYLOAD_TYPE_REQ:
    case PAYLOAD_TYPE_RESPONSE:
    case PAYLOAD_TYPE_TXT_MSG:
    case PAYLOAD_TYPE_PATH:
      return 2 + CIPHER_MAC_SIZE + 1;  // destination, source, MAC, ciphertext
    case PAYLOAD_TYPE_ACK:
      return sizeof(uint32_t);
    case PAYLOAD_TYPE_ADVERT:
      return PUB_KEY_SIZE + sizeof(uint32_t) + SIGNATURE_SIZE;
    case PAYLOAD_TYPE_GRP_TXT:
    case PAYLOAD_TYPE_GRP_DATA:
      return 1 + CIPHER_MAC_SIZE + 1;  // channel, MAC, ciphertext
    case PAYLOAD_TYPE_ANON_REQ:
      return 1 + PUB_KEY_SIZE + CIPHER_MAC_SIZE + 1;
    case PAYLOAD_TYPE_TRACE:
      return sizeof(uint32_t) * 2 + 1;  // tag, auth code, flags
    case PAYLOAD_TYPE_MULTIPART:
    case PAYLOAD_TYPE_CONTROL:
      return 1;
    default:
      return 0;
  }
}

bool Packet::hasValidPayloadShape() const {
  return payload_len <= sizeof(payload) &&
         payload_len >= minimumPayloadLength(getPayloadType());
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
  if (src == NULL || len < 2) return false;

  size_t i = 0;
  header = src[i++];
  if (getPayloadVer() > PAYLOAD_VER_1) return false;
  if (hasTransportCodes()) {
    if ((size_t)len - i < sizeof(transport_codes)) return false;
    memcpy(&transport_codes[0], &src[i], 2); i += 2;
    memcpy(&transport_codes[1], &src[i], 2); i += 2;
  } else {
    transport_codes[0] = transport_codes[1] = 0;
  }
  if (i >= len) return false;
  path_len = src[i++];
  if (!isValidPathLen(path_len)) return false;   // bad encoding

  size_t bl = getPathByteLen();
  if (bl > (size_t)len - i) return false;
  memcpy(path, &src[i], bl); i += bl;

  payload_len = len - i;
  if (payload_len > sizeof(payload)) return false;  // bad encoding
  memcpy(payload, &src[i], payload_len); //i += payload_len;
  return hasValidPayloadShape();
}

}
