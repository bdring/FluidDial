// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.

#ifdef USE_WIFI

#    include "Scene.h"
#    include "Drawing.h"
#    include "Button.h"
#    include "WiFiConnection.h"
#    include "ESPNowPairingScene.h"

extern void first_boot_complete();

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BTN_W = 160;
static constexpr int BTN_H = 40;
static constexpr int BTN_X = (240 - BTN_W) / 2;

static constexpr int UART_BTN_Y    = 60;
static constexpr int WIFI_BTN_Y    = 112;
static constexpr int ESPNOW_BTN_Y  = 164;

class FirstBootScene : public Scene {
    uint32_t _entry_ms = 0;
    Button   wifiBtn, uartBtn, espnowBtn;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

    void onWifiPress() {
        if (!selectable()) return;
        wifi_set_transport(TransportMode::WIFI);
        first_boot_complete();
    }

    void onUartPress() {
        if (!selectable()) return;
        wifi_set_transport(TransportMode::UART);
        first_boot_complete();
    }

    void onEspNowPress() {
        if (!selectable()) return;
        wifi_set_transport(TransportMode::ESPNOW);
        first_boot_complete();
    }

public:
    FirstBootScene() : Scene("Setup") {}

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        set_disconnected_state();  // prevent dispatch_events() redirect to menuScene
    }

    void onGreenButtonPress() override {
        if (!selectable()) return;
        wifi_set_transport(TransportMode::WIFI);
        first_boot_complete();
    }

    void onRedButtonPress() override {
        if (!selectable()) return;
        wifi_set_transport(TransportMode::UART);
        first_boot_complete();
    }

    bool showButtons() override { return false; }

    void onDialButtonPress() override {}

    void reDisplay() override {
        background();
        drawMenuTitle("Setup");

        if (round_display) {
            drawRect(60, 28, 120, 1, 0, DARKGREY);
            centered_text("Connection Mode", 48, ORANGE, TINY);
        } else {
            drawRect(55, 22, 130, 1, 0, DARKGREY);
            centered_text("Connection Mode", 44, ORANGE, SMALL);
        }

        int uart_y   = round_display ? UART_BTN_Y   + 4  : UART_BTN_Y;
        int wifi_y   = round_display ? WIFI_BTN_Y   - 4  : WIFI_BTN_Y;
        int espnow_y = round_display ? ESPNOW_BTN_Y - 12 : ESPNOW_BTN_Y;

        uartBtn.set(BTN_X, uart_y, BTN_W, BTN_H, "Wired (UART)",
                    0x001a4d, 0x4da6ff, 0x4da6ff, [this]() { onUartPress(); });

        wifiBtn.set(BTN_X, wifi_y, BTN_W, BTN_H, "WiFi",
                    0x003300, 0x66ff66, 0x66ff66, [this]() { onWifiPress(); });

        espnowBtn.set(BTN_X, espnow_y, BTN_W, BTN_H, "ESP-NOW (no router)",
                      0x1a0033, 0xcc66ff, 0xcc66ff, [this]() { onEspNowPress(); });

        if (!round_display) {
            centered_text("This can be changed later", 218, DARKGREY, TINY);
        }
        refreshDisplay();
    }

    void onTouchClick() override {
        wifiBtn.handleTouch(touchX, touchY);
        uartBtn.handleTouch(touchX, touchY);
        espnowBtn.handleTouch(touchX, touchY);
    }
};

FirstBootScene firstBootScene;

#endif  // USE_WIFI
