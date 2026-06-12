#pragma once

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include "UnifiedTransportManager.h"

/**
 * UnifiedTransportScreen
 *
 * A UIScreen that lists available transports and lets the user select
 * the active one using the device buttons. Integrates into the existing
 * page-based home screen system.
 *
 * Navigation:
 *   - KEY_NEXT / KEY_RIGHT: Move selection down
 *   - KEY_PREV / KEY_LEFT:  Move selection up
 *   - KEY_ENTER:             Confirm selection (switch transport)
 *
 * The screen only shows transports registered with the manager.
 * Unavailable transports (not built for this board) are not displayed.
 */

class UnifiedTransportScreen : public UIScreen {
public:
    UnifiedTransportScreen(UnifiedTransportManager* manager)
        : _manager(manager),
          _selected_index(0),
          _last_known_active(TRANSPORT_NONE)
    {
        // Calculate how many transports we have
        int count = _manager->getNumTransports();
        if (count > 0) {
            // Start selection on the currently active transport
            TransportType active = _manager->getActiveTransport();
            for (int i = 0; i < count; i++) {
                if (_manager->getTransportByIndex(i) == active) {
                    _selected_index = i;
                    break;
                }
            }
        }
    }

    int render(DisplayDriver& display) override {
        int y = 14;  // Start below status bar area
        int count = _manager->getNumTransports();
        TransportType current = _manager->getActiveTransport();

        display.setTextSize(1);
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 0, "Connection Mode");

        for (int i = 0; i < count; i++) {
            TransportType type = _manager->getTransportByIndex(i);
            bool is_active = (type == current);
            bool is_selected = (i == _selected_index);

            if (is_selected) {
                display.setColor(DisplayDriver::DARK);
                display.fillRect(0, y, display.width(), 10);
                display.setColor(DisplayDriver::LIGHT);
            } else if (is_active) {
                display.setColor(DisplayDriver::GREEN);
            } else {
                display.setColor(DisplayDriver::LIGHT);
            }

            const char* name = transportTypeName(type);
            display.setCursor(4, y);

            if (is_active) {
                display.print("> ");
            } else if (is_selected) {
                display.print("* ");
            } else {
                display.print("  ");
            }

            display.print(name);

            if (is_active) {
                display.print(" [ACTIVE]");
            }

            y += 11;
        }

        // Instructions at bottom
        if (_manager->getNumTransports() > 1) {
            display.setColor(DisplayDriver::LIGHT);
            display.setTextSize(1);
            display.drawTextCentered(display.width() / 2, display.height() - 10,
                "ENTER to switch");
        }

        return 3000;  // Refresh every 3 seconds (to update [ACTIVE] label)
    }

    bool handleInput(char c) override {
        int count = _manager->getNumTransports();
        if (count <= 1) return true;  // Nothing to switch

        switch (c) {
            case KEY_NEXT:
            case KEY_RIGHT:
            case KEY_DOWN:
                _selected_index = (_selected_index + 1) % count;
                return true;

            case KEY_PREV:
            case KEY_LEFT:
            case KEY_UP:
                _selected_index = (_selected_index + count - 1) % count;
                return true;

            case KEY_ENTER:
            case KEY_SELECT: {
                TransportType selected = _manager->getTransportByIndex(_selected_index);
                TransportType current = _manager->getActiveTransport();
                if (selected != current) {
                    _manager->selectTransport(selected);
                }
                return true;
            }

            default:
                return false;
        }
    }

    void poll() override {
        // Detect if active transport was changed externally
        TransportType current = _manager->getActiveTransport();
        if (current != _last_known_active) {
            _last_known_active = current;
            // Update selection to match
            int count = _manager->getNumTransports();
            for (int i = 0; i < count; i++) {
                if (_manager->getTransportByIndex(i) == current) {
                    _selected_index = i;
                    break;
                }
            }
        }
    }

private:
    UnifiedTransportManager* _manager;
    int _selected_index;
    TransportType _last_known_active;
};
