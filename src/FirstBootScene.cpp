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

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int CARD_X = 28;
static constexpr int CARD_W = 184;
static constexpr int CARD_H = 66;

static constexpr int WIFI_CARD_Y = 50;
static constexpr int UART_CARD_Y = 124;

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

public:
    FirstBootScene() : Scene("Setup") {}

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        set_disconnected_state();  // prevent dispatch_events() redirect to menuScene
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

        // ── WiFi option ────────────────────────────────────────────────────────
        drawOutlinedRect(CARD_X, WIFI_CARD_Y, CARD_W, CARD_H, NAVY, GREEN);
        centered_text("WiFi",               WIFI_CARD_Y + 30, GREEN,     MEDIUM);

        // ── UART option ────────────────────────────────────────────────────────
        drawOutlinedRect(CARD_X, UART_CARD_Y, CARD_W, CARD_H, NAVY, RED);
        centered_text("UART",               UART_CARD_Y + 30, RED,       MEDIUM);

        // ── Footer ─────────────────────────────────────────────────────────────
        centered_text("Change be changed later", 200, DARKGREY, TINY);

        drawButtonLegends("UART", "WiFi", "");
        refreshDisplay();
    }
};

FirstBootScene firstBootScene;

#endif  // ARDUINO
