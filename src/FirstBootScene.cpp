// 2026 - Figamore
// FirstBootScene.cpp — one-time setup wizard shown on first boot.
//
// Asks the user to choose a transport (WiFi/Telnet or wired UART; ESP-NOW too
// when built with -DUSE_ESPNOW). The choice is saved to NVS, then transitions
// directly to the appropriate scene.

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
    int      _selected = 0;  // 0=Wired, 1=WiFi, 2=ESP-NOW
    Button   wifiBtn, uartBtn, espnowBtn;

    bool selectable() { return (millis() - _entry_ms) >= 800; }

    int numChoices() {
#ifdef USE_ESPNOW
        return 3;
#else
        return 2;
#endif
    }

    void confirmSelection() {
        if (!selectable()) return;
        static const TransportMode modes[] = {
            TransportMode::UART,
            TransportMode::WIFI,
#ifdef USE_ESPNOW
            TransportMode::ESPNOW,
#endif
        };
        wifi_set_transport(modes[_selected]);
        first_boot_complete();
    }

public:
    FirstBootScene() : Scene("Setup", 4) {}

    void onEntry(void* arg = nullptr) override {
        _entry_ms = millis();
        _selected = 0;
        set_disconnected_state();  // prevent dispatch_events() redirect to menuScene
    }

    void onEncoder(int delta) override {
        int n = numChoices();
        _selected = ((_selected + delta) % n + n) % n;
        reDisplay();
    }

    void onGreenButtonPress() override { confirmSelection(); }

    bool showButtons() override { return true; }

    void onDialButtonPress() override {}

    void reDisplay() override {
        background();
        centered_text("Setup", 16);

        if (round_display) {
            drawRect(60, 28, 120, 1, 0, DARKGREY);
            centered_text("Connection Mode", 48, ORANGE, TINY);
        } else {
            drawRect(55, 22, 130, 1, 0, DARKGREY);
            centered_text("Connection Mode", 44, ORANGE, SMALL);
        }

        int uart_y   = round_display ? UART_BTN_Y   + 4  : UART_BTN_Y;
        int wifi_y   = round_display ? WIFI_BTN_Y   - 4  : WIFI_BTN_Y;
#ifdef USE_ESPNOW
        int espnow_y = round_display ? ESPNOW_BTN_Y - 12 : ESPNOW_BTN_Y;
#endif

        constexpr int SEL_EXPAND = 8;   // extra px on each side when selected
        constexpr int DOT_R      = 4;
        constexpr int DOT_OFFSET = SEL_EXPAND + DOT_R + 4;  // from selected btn left edge

        auto btnX = [&](int idx) { return _selected == idx ? BTN_X - SEL_EXPAND : BTN_X; };
        auto btnW = [&](int idx) { return _selected == idx ? BTN_W + SEL_EXPAND * 2 : BTN_W; };

        uartBtn.set(btnX(0), uart_y, btnW(0), BTN_H, "Wired",
                    0x001a4d, 0x4da6ff, 0x4da6ff,
                    [this]() { _selected = 0; confirmSelection(); });

        wifiBtn.set(btnX(1), wifi_y, btnW(1), BTN_H, "WiFi",
                    0x003300, 0x66ff66, 0x66ff66,
                    [this]() { _selected = 1; confirmSelection(); });

#ifdef USE_ESPNOW
        espnowBtn.set(btnX(2), espnow_y, btnW(2), BTN_H, "ESP-NOW",
                      0x1a0033, 0xcc66ff, 0xcc66ff,
                      [this]() { _selected = 2; confirmSelection(); });
#endif

        // Dot indicator to the left of the selected button
        int sel_ys[] = { uart_y, wifi_y
#ifdef USE_ESPNOW
            , espnow_y
#endif
        };
        int dot_x = (BTN_X - SEL_EXPAND) - DOT_OFFSET;
        int dot_y = sel_ys[_selected] + BTN_H / 2;
        canvas.fillCircle(dot_x, dot_y, DOT_R, WHITE);

        // if (!round_display) {
        //     centered_text("This can be changed later", 218, DARKGREY, TINY);
        // }
        
        drawButtonLegends("", "Select", "");
        refreshDisplay();
    }

    void onTouchClick() override {
        uartBtn.handleTouch(touchX, touchY);
        wifiBtn.handleTouch(touchX, touchY);
#ifdef USE_ESPNOW
        espnowBtn.handleTouch(touchX, touchY);
#endif
    }
};

FirstBootScene firstBootScene;

#endif  // USE_WIFI
