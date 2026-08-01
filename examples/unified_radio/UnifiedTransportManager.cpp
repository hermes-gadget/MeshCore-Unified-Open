#include "UnifiedTransportManager.h"

UnifiedTransportManager::UnifiedTransportManager()
    : num_transports(0),
      active_type(TRANSPORT_NONE),
      default_type(TRANSPORT_ALL),
      active_iface(nullptr),
      next_poll_index(0),
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
    transports[num_transports].has_received_frame = false;
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
    if (type == TRANSPORT_ALL) return num_transports > 0;
    return findIndex(type) >= 0;
}

bool UnifiedTransportManager::isEntryConnected(const TransportEntry& entry) const {
    // ArduinoSerialInterface cannot observe whether a USB/UART host is
    // attached and therefore always returns true. Do not let its presence make
    // the whole unified device look connected from boot; a complete inbound
    // frame proves that a serial companion is actually present.
    if (entry.type == TRANSPORT_USB) return entry.has_received_frame;
    return entry.iface->isConnected();
}

TransportType UnifiedTransportManager::getTransportByIndex(int index) const {
    if (index < 0) return TRANSPORT_NONE;
    if (num_transports > 1) {
        if (index == 0) return TRANSPORT_ALL;
        index--;
    }
    if (index >= num_transports) return TRANSPORT_NONE;
    return transports[index].type;
}

void UnifiedTransportManager::enableAll() {
    for (int i = 0; i < num_transports; i++) transports[i].iface->enable();
}

void UnifiedTransportManager::disableAll() {
    for (int i = 0; i < num_transports; i++) transports[i].iface->disable();
}

bool UnifiedTransportManager::selectTransport(TransportType type) {
    // "All" and the sole physical transport are equivalent on USB-only
    // boards. Normalize the state so selector APIs still report USB as active.
    if (type == TRANSPORT_ALL && num_transports == 1) {
        type = transports[0].type;
    }
    if (type == active_type) return true;  // Already on this transport

    int idx = findIndex(type);
    if (type != TRANSPORT_ALL && idx < 0) return false;
    if (type == TRANSPORT_ALL && num_transports < 1) return false;

    // Gracefully stop the current transport
    disableAll();

    // Update internal state
    active_type = type;
    active_iface = type == TRANSPORT_ALL ? nullptr : transports[idx].iface;
    next_poll_index = 0;

    // Reset buffers and enable the new transport
    if (type == TRANSPORT_ALL) enableAll();
    else active_iface->enable();

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
        if (hasTransport(saved_type) && saved_type != TRANSPORT_NONE) {
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
    if (active_type == TRANSPORT_ALL) enableAll();
    else if (active_iface) active_iface->enable();
}

void UnifiedTransportManager::disable() {
    if (active_type == TRANSPORT_ALL) disableAll();
    else if (active_iface) active_iface->disable();
}

bool UnifiedTransportManager::isEnabled() const {
    if (active_type == TRANSPORT_ALL) {
        for (int i = 0; i < num_transports; i++) {
            if (transports[i].iface->isEnabled()) return true;
        }
        return false;
    }
    return active_iface ? active_iface->isEnabled() : false;
}

bool UnifiedTransportManager::isConnected() const {
    if (active_type == TRANSPORT_ALL) {
        for (int i = 0; i < num_transports; i++) {
            if (isEntryConnected(transports[i])) return true;
        }
        return false;
    }
    int index = findIndex(active_type);
    return index >= 0 ? isEntryConnected(transports[index]) : false;
}

bool UnifiedTransportManager::isWriteBusy() const {
    if (active_type == TRANSPORT_ALL) {
        for (int i = 0; i < num_transports; i++) {
            if (transports[i].iface->isWriteBusy()) return true;
        }
        return false;
    }
    return active_iface ? active_iface->isWriteBusy() : false;
}

size_t UnifiedTransportManager::writeFrame(const uint8_t src[], size_t len) {
    if (active_type == TRANSPORT_ALL) {
        bool wrote = false;
        for (int i = 0; i < num_transports; i++) {
            if (isEntryConnected(transports[i]) &&
                transports[i].iface->writeFrame(src, len) == len) {
                wrote = true;
            }
        }
        return wrote ? len : 0;
    }
    if (!active_iface) return 0;
    return active_iface->writeFrame(src, len);
}

size_t UnifiedTransportManager::checkRecvFrame(uint8_t dest[]) {
    if (active_type == TRANSPORT_ALL) {
        for (int checked = 0; checked < num_transports; checked++) {
            int index = (next_poll_index + checked) % num_transports;
            size_t len = transports[index].iface->checkRecvFrame(dest);
            if (len > 0) {
                transports[index].has_received_frame = true;
                next_poll_index = (index + 1) % num_transports;
                return len;
            }
        }
        if (num_transports > 0) next_poll_index = (next_poll_index + 1) % num_transports;
        return 0;
    }
    if (!active_iface) return 0;
    size_t len = active_iface->checkRecvFrame(dest);
    if (len > 0) {
        int index = findIndex(active_type);
        if (index >= 0) transports[index].has_received_frame = true;
    }
    return len;
}
