#pragma once

#include <Arduino.h>
#include <helpers/BaseSerialInterface.h>
#include "UnifiedTransportConfig.h"

/**
 * UnifiedTransportManager
 *
 * A thin proxy layer that presents multiple transport interfaces (USB, BLE,
 * WiFi) as a single BaseSerialInterface. The active transport can be changed
 * at runtime without reflashing. Gracefully stops the old transport before
 * starting the new one.
 *
 * The manager IS a BaseSerialInterface — call myMesh.startInterface(manager)
 * once and all delegation happens transparently. No changes to MyMesh or
 * BaseChatMesh are needed.
 *
 * Persistence: Transport mode is saved to a file on the filesystem.
 * On boot, the saved mode is loaded. If the saved mode is unavailable on this
 * board (e.g. BLE firmware on a non-BLE board), the safe default is used.
 *
 * Maximum transports is set at compile time. Add new transports by extending
 * the TransportType enum and calling addTransport() in main.cpp.
 */

#define MAX_TRANSPORTS 4

class UnifiedTransportManager : public BaseSerialInterface {
public:
    UnifiedTransportManager();

    /**
     * Register a transport for runtime selection.
     * @param type   The TransportType identifier
     * @param iface  Pointer to the BaseSerialInterface instance
     */
    void addTransport(TransportType type, BaseSerialInterface* iface);

    /**
     * Set the default transport (used on first boot / when persisted mode unavailable).
     */
    void setDefaultTransport(TransportType type);

    /**
     * Switch the active transport. Gracefully disables the current
     * transport, then enables the new one. No-op if already on the
     * requested transport.
     * @return true if the switch succeeded
     */
    bool selectTransport(TransportType type);

    /**
     * Get the currently active transport type.
     */
    TransportType getActiveTransport() const { return active_type; }

    /**
     * Get the number of registered transports.
     */
    int getNumTransports() const { return num_transports; }

    /**
     * Get the TransportType at a given index (for iterating in UI).
     */
    TransportType getTransportByIndex(int index) const;

    /**
     * Check if a transport type is registered.
     */
    bool hasTransport(TransportType type) const;

    /**
     * Get human-readable name for the active transport.
     */
    const char* getActiveTransportName() const {
        return transportTypeName(active_type);
    }

    // -------- Persistence --------

    /**
     * Save the current transport selection to a file on the filesystem.
     * Uses the DataStore's filesystem path via a provided function pointer.
     * @param save_fn  Callback that writes a single byte to persistent storage
     */
    using SaveCallback = bool (*)(uint8_t value);
    using LoadCallback = bool (*)(uint8_t* value);

    void setPersistenceCallbacks(SaveCallback save_fn, LoadCallback load_fn) {
        _save_fn = save_fn;
        _load_fn = load_fn;
    }

    /**
     * Load saved transport from persistent storage.
     * If no saved value or it's unavailable, uses the default.
     */
    void loadPersistedTransport();

    /**
     * Save the current transport to persistent storage.
     */
    void persistActiveTransport();

    // -------- BaseSerialInterface (delegating to active transport) --------

    void enable() override;
    void disable() override;
    bool isEnabled() const override;
    bool isConnected() const override;
    bool isWriteBusy() const override;
    size_t writeFrame(const uint8_t src[], size_t len) override;
    size_t checkRecvFrame(uint8_t dest[]) override;

private:
    struct TransportEntry {
        TransportType type;
        BaseSerialInterface* iface;
    };

    TransportEntry transports[MAX_TRANSPORTS];
    int num_transports;
    TransportType active_type;
    TransportType default_type;
    BaseSerialInterface* active_iface;

    // Persistence callbacks
    SaveCallback _save_fn;
    LoadCallback _load_fn;

    int findIndex(TransportType type) const;
};
