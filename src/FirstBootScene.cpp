// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.
//
// Asks the user to choose WiFi (WebSocket) or UART (serial cable).
// Choice is saved to NVS, then the ESP restarts into normal operation.

#ifdef ARDUINO

#include "Scene.h"
#include "Drawing.h"
#include "WiFiConnection.h"

#include <Esp.h>

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

public:
    FirstBootScene() : Scene("Setup") {}

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        reDisplay();
    }

    void onGreenButtonPress() override {
        if (!selectable()) return;
        wifi_set_uart_mode(false);  // WiFi / WebSocket
        ESP.restart();
    }

    void onRedButtonPress() override {
        if (!selectable()) return;
        wifi_set_uart_mode(true);   // UART / serial cable
        ESP.restart();
    }

    // Dial is intentionally a no-op — user must pick a transport.
    void onDialButtonPress() override {}

    void reDisplay() override {
        background();
        drawMenuTitle("Setup");
        drawRect(55, 22, 130, 1, 0, DARKGREY);

        centered_text("First boot — choose how", 42, LIGHTGREY, TINY);
        centered_text("FluidDial talks to FluidNC:", 56, LIGHTGREY, TINY);

        // WiFi option — outlined in GREEN to match the Green button
        drawOutlinedRect(25, 72, 190, 46, NAVY, GREEN);
        centered_text("WiFi  (Green button)", 90, GREEN, SMALL);
        centered_text("Wireless via WebSocket", 106, LIGHTGREY, TINY);

        // UART option — outlined in RED to match the Red button
        drawOutlinedRect(25, 124, 190, 46, NAVY, RED);
        centered_text("UART  (Red button)", 142, RED, SMALL);
        centered_text("Wired serial cable", 158, LIGHTGREY, TINY);

        centered_text("You can change this later", 182, DARKGREY, TINY);
        centered_text("in Settings.", 196, DARKGREY, TINY);

        drawButtonLegends("UART", "WiFi", "");
        refreshDisplay();
    }
};

FirstBootScene firstBootScene;

#endif  // ARDUINO
