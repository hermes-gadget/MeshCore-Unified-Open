#include "UnifiedTransportManager.h"

UnifiedTransportManager::UnifiedTransportManager()
    : num_transports(0),
      active_type(TRANSPORT_NONE),
      default_type(TRANSPORT_USB),
      active_iface(nullptr),
      _save_fn(nullptr),
      _load_fn(nullptr)
{
}

void UnifiedTransportManager::addTransport(TransportType type, BaseSerialInterface* iface) {
    if (num_transports >= MAX_TRANSPORTS) return;
    if (!iface) return;
    // Don't add duplicates
    for (int i = 0; i < num_transports; i++) {
        if (transports[i].type == type) return;
    }
    transports[num_transports].type = type;
    transports[num_transports].iface = iface;
    num_transports++;
}

void UnifiedTransportManager::setDefaultTransport(TransportType type) {
    default_type = type;
}

int UnifiedTransportManager::findIndex(TransportType type) const {
    for (int i = 0; i < num_transports; i++) {
        if (transports[i].type == type) return i;
    }
    return -1;
}

bool UnifiedTransportManager::hasTransport(TransportType type) const {
    return findIndex(type) >= 0;
}

TransportType UnifiedTransportManager::getTransportByIndex(int index) const {
    if (index < 0 || index >= num_transports) return TRANSPORT_NONE;
    return transports[index].type;
}

bool UnifiedTransportManager::selectTransport(TransportType type) {
    if (type == active_type) return true;  // Already on this transport

    int idx = findIndex(type);
    if (idx < 0) return false;  // Transport not registered

    // Gracefully stop the current transport
    if (active_iface != nullptr) {
        // Flush any pending I/O by disabling the transport
        active_iface->disable();
    }

    // Update internal state
    active_type = type;
    active_iface = transports[idx].iface;

    // Reset buffers and enable the new transport
    active_iface->enable();

    // Persist the selection
    persistActiveTransport();

    return true;
}

void UnifiedTransportManager::loadPersistedTransport() {
    if (_load_fn == nullptr) {
        // No persistence available — use default
        selectTransport(default_type);
        return;
    }

    uint8_t saved_value;
    if (_load_fn(&saved_value)) {
        TransportType saved_type = static_cast<TransportType>(saved_value);
        if (findIndex(saved_type) >= 0 && saved_type != TRANSPORT_NONE) {
            selectTransport(saved_type);
            return;
        }
    }

    // No saved value or saved transport unavailable — use default
    selectTransport(default_type);
}

void UnifiedTransportManager::persistActiveTransport() {
    if (_save_fn == nullptr) return;
    if (active_type == TRANSPORT_NONE) return;
    _save_fn(static_cast<uint8_t>(active_type));
}

// -------- BaseSerialInterface delegation --------

void UnifiedTransportManager::enable() {
    if (active_iface) active_iface->enable();
}

void UnifiedTransportManager::disable() {
    if (active_iface) active_iface->disable();
}

bool UnifiedTransportManager::isEnabled() const {
    return active_iface ? active_iface->isEnabled() : false;
}

bool UnifiedTransportManager::isConnected() const {
    return active_iface ? active_iface->isConnected() : false;
}

bool UnifiedTransportManager::isWriteBusy() const {
    return active_iface ? active_iface->isWriteBusy() : false;
}

size_t UnifiedTransportManager::writeFrame(const uint8_t src[], size_t len) {
    if (!active_iface) return 0;
    return active_iface->writeFrame(src, len);
}

size_t UnifiedTransportManager::checkRecvFrame(uint8_t dest[]) {
    if (!active_iface) return 0;
    return active_iface->checkRecvFrame(dest);
}
