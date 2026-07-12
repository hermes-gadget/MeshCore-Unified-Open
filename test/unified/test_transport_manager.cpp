#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "UnifiedTransportManager.h"

class FakeTransport : public BaseSerialInterface {
public:
    bool enabled = false;
    bool connected = true;
    bool busy = false;
    uint8_t pending = 0;
    int writes = 0;

    void enable() override { enabled = true; }
    void disable() override { enabled = false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return connected; }
    bool isWriteBusy() const override { return busy; }
    size_t writeFrame(const uint8_t[], size_t len) override {
        if (!enabled || !connected) return 0;
        writes++;
        return len;
    }
    size_t checkRecvFrame(uint8_t dest[]) override {
        if (!enabled || pending == 0) return 0;
        dest[0] = pending;
        pending = 0;
        return 1;
    }
};

static uint8_t persisted = TRANSPORT_NONE;
static bool saveMode(uint8_t value) { persisted = value; return true; }
static bool loadMode(uint8_t* value) { *value = persisted; return persisted != TRANSPORT_NONE; }

int main() {
    // The Arduino USB/UART adapter always reports connected. The manager must
    // wait for real serial traffic before exposing that as an app connection.
    {
        FakeTransport serial;
        UnifiedTransportManager serial_manager;
        serial_manager.addTransport(TRANSPORT_USB, &serial);
        assert(serial_manager.selectTransport(TRANSPORT_ALL));
        assert(serial_manager.getActiveTransport() == TRANSPORT_USB);
        assert(!serial_manager.isConnected());
        uint8_t serial_received[MAX_FRAME_SIZE] = {};
        serial.pending = 7;
        assert(serial_manager.checkRecvFrame(serial_received) == 1);
        assert(serial_manager.isConnected());
    }

    FakeTransport usb, ble, wifi;
    UnifiedTransportManager manager;
    manager.addTransport(TRANSPORT_USB, &usb);
    manager.addTransport(TRANSPORT_BLE, &ble);
    manager.addTransport(TRANSPORT_WIFI, &wifi);

    assert(manager.getNumTransports() == 4);
    assert(manager.getTransportByIndex(0) == TRANSPORT_ALL);
    assert(manager.getTransportByIndex(3) == TRANSPORT_WIFI);
    assert(manager.hasTransport(TRANSPORT_ALL));

    manager.setPersistenceCallbacks(saveMode, loadMode);
    manager.setDefaultTransport(TRANSPORT_ALL);
    manager.loadPersistedTransport();
    assert(manager.getActiveTransport() == TRANSPORT_ALL);
    assert(usb.enabled && ble.enabled && wifi.enabled);
    assert(persisted == TRANSPORT_ALL);

    uint8_t frame[] = {1, 2, 3};
    assert(manager.writeFrame(frame, sizeof(frame)) == sizeof(frame));
    assert(usb.writes == 0 && ble.writes == 1 && wifi.writes == 1);

    uint8_t received[MAX_FRAME_SIZE] = {};
    usb.pending = 11;
    ble.pending = 22;
    assert(manager.checkRecvFrame(received) == 1 && received[0] == 11);
    assert(manager.checkRecvFrame(received) == 1 && received[0] == 22);
    assert(manager.writeFrame(frame, sizeof(frame)) == sizeof(frame));
    assert(usb.writes == 1 && ble.writes == 2 && wifi.writes == 2);

    assert(manager.selectTransport(TRANSPORT_BLE));
    assert(!usb.enabled && ble.enabled && !wifi.enabled);
    assert(manager.getActiveTransport() == TRANSPORT_BLE);
    assert(persisted == TRANSPORT_BLE);
    assert(manager.writeFrame(frame, sizeof(frame)) == sizeof(frame));
    assert(ble.writes == 3 && usb.writes == 1 && wifi.writes == 2);

    manager.disable();
    assert(!manager.isEnabled());
    manager.enable();
    assert(manager.isEnabled());
    return 0;
}
