// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.
//
// Asks the user to choose WiFi (WebSocket) or UART (serial cable).
// Choice is saved to NVS, then transitions directly to the appropriate scene.

#ifdef USE_WIFI

#    include "Scene.h"
#    include "Drawing.h"
#    include "Button.h"
#    include "WiFiConnection.h"

extern void first_boot_complete();

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BTN_W = 160;
static constexpr int BTN_H = 45;
static constexpr int BTN_X = (240 - BTN_W) / 2;  // Centered

static constexpr int UART_BTN_Y = 95;
static constexpr int WIFI_BTN_Y = 165;

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;
    Button wifiBtn, uartBtn;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

    void onWifiPress() {
        if (!selectable())
            return;
        wifi_set_uart_mode(false);  // WiFi / WebSocket
        first_boot_complete();
    }

    void onUartPress() {
        if (!selectable())
            return;
        wifi_set_uart_mode(true);  // UART / serial cable
        first_boot_complete();
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
        first_boot_complete();
    }

    void onRedButtonPress() override {
        if (!selectable())
            return;
        wifi_set_uart_mode(true);  // UART / serial cable
        first_boot_complete();
    }

    bool showButtons() override { return false; }

    // Dial is intentionally a no-op — user must pick a transport.
    void onDialButtonPress() override {}

    void reDisplay() override {
        background();
        drawMenuTitle("Setup");
        drawRect(55, 22, 130, 1, 0, DARKGREY);

        // ── Main question ──────────────────────────────────────────────────────
        wrapped_text("Connection Mode", 70, 200, ORANGE, SMALL);

        // ── Buttons (stacked vertically) ────────────────────────────────────────
        uartBtn.set(BTN_X, UART_BTN_Y, BTN_W, BTN_H, "Wired",
                    0x001a4d, 0x4da6ff, 0x4da6ff, [this]() { onUartPress(); });

        wifiBtn.set(BTN_X, WIFI_BTN_Y, BTN_W, BTN_H, "WiFi",
                    0x003300, 0x66ff66, 0x66ff66, [this]() { onWifiPress(); });

        // ── Footer ─────────────────────────────────────────────────────────────
        centered_text("This can be changed later", 230, DARKGREY, TINY);
 
        refreshDisplay();
    }

    void onTouchClick() override {
        wifiBtn.handleTouch(touchX, touchY);
        uartBtn.handleTouch(touchX, touchY);
    }
};

FirstBootScene firstBootScene;

#endif  // USE_WIFI
