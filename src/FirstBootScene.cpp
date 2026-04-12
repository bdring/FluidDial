// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.
//
// Asks the user to choose WiFi (WebSocket) or UART (serial cable).
// Choice is saved to NVS, then the ESP restarts into normal operation.

#ifdef ARDUINO

#    include "Scene.h"
#    include "Drawing.h"
#    include "Button.h"
#    include "WiFiConnection.h"

#    include <Esp.h>

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BTN_W = 160;
static constexpr int BTN_H = 45;
static constexpr int BTN_X = (240 - BTN_W) / 2;  // Centered

static constexpr int UART_BTN_Y = 100;
static constexpr int WIFI_BTN_Y = 160;

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;
    Button wifiBtn, uartBtn;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

    void onWifiPress() {
        if (!selectable())
            return;
        wifi_set_uart_mode(false);  // WiFi / WebSocket
        ESP.restart();
    }

    void onUartPress() {
        if (!selectable())
            return;
        wifi_set_uart_mode(true);  // UART / serial cable
        ESP.restart();
    }

public:
    FirstBootScene() : Scene("Setup") {}

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        set_disconnected_state();  // prevent dispatch_events() redirect to menuScene
    }

    void onGreenButtonPress() override {
        if (!selectable())
            return;
        wifi_set_uart_mode(false);  // WiFi / WebSocket
        ESP.restart();
    }

    void onRedButtonPress() override {
        if (!selectable())
            return;
        wifi_set_uart_mode(true);  // UART / serial cable
        ESP.restart();
    }

    // Dial is intentionally a no-op — user must pick a transport.
    void onDialButtonPress() override {}

    void reDisplay() override {
        background();
        drawMenuTitle("Setup");
        drawRect(55, 22, 130, 1, 0, DARKGREY);

        // ── Main question ──────────────────────────────────────────────────────
        wrapped_text("Select connection mode", 70, 200, WHITE, SMALL);

        // ── Buttons (stacked vertically) ────────────────────────────────────────
        uartBtn.set(BTN_X, UART_BTN_Y, BTN_W, BTN_H, "← UART",
                    0x001a4d, 0x4da6ff, 0x4da6ff, [this]() { onUartPress(); });

        wifiBtn.set(BTN_X, WIFI_BTN_Y, BTN_W, BTN_H, "WiFi →",
                    0x003300, 0x66ff66, 0x66ff66, [this]() { onWifiPress(); });

        // ── Footer ─────────────────────────────────────────────────────────────
        centered_text("This can be changed later", 230, DARKGREY, TINY);

        // Hide physical button area (cover with black rectangles)
        drawRect(0, 270, 80, 50, 0, BLACK);      // Red button area
        drawRect(80, 270, 80, 50, 0, BLACK);     // Orange button area
        drawRect(160, 270, 80, 50, 0, BLACK);    // Green button area
 
        refreshDisplay();
    }

    void onTouchClick() override {
        wifiBtn.handleTouch(touchX, touchY);
        uartBtn.handleTouch(touchX, touchY);
    }
};

FirstBootScene firstBootScene;

#endif  // ARDUINO
