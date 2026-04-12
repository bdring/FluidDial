// 2026 - Figamore
// Use of this source code is governed by a GPLv3 license.
//
// Shows connection status and offers AP-mode setup.

#ifdef ARDUINO

#include "WiFiSetupScene.h"
#include "WiFiConnection.h"
#include "Drawing.h"
#include "Menu.h"
#include "System.h"
#include "Button.h"
#include "DisplaySettingsScene.h"

extern Scene     menuScene;
extern const char* git_info;

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BX = 20,  BY  = 28,  BW  = 200, BH  = 34;  // status badge
static constexpr int CX = 15,  CW  = 210, CH  = 28,  CI  = 8;   // info cards
static constexpr int SBX = 20, SBY = 166, SBW = 200, SBH = 36;  // switch button

static constexpr int CARD_Y0    = 68;
static constexpr int CARD_PITCH = CH + 4;  // 32 px per card row

// ─── Card drawing helpers ──────────────────────────────────────────────────────

static void drawCard(int y, const char* label, const char* value, int val_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI,      mid, DARKGREY,  TINY,  middle_left);
    text(value, CX + CW - CI, mid, val_color, SMALL, middle_right);
}

static void drawCardAuto(int y, const char* label, const char* value, int val_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI, mid, DARKGREY, TINY, middle_left);
    static constexpr int VALUE_W = 130;
    auto_text(std::string(value), CX + CW - CI, mid, VALUE_W, val_color, SMALL, middle_right);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────

static const char* signal_str(int bars) {
    switch (bars) {
        case 4:  return "Excellent";
        case 3:  return "Good";
        case 2:  return "Fair";
        case 1:  return "Weak";
        default: return "None";
    }
}

// ─── Event handlers ───────────────────────────────────────────────────────────

void WiFiSetupScene::onEntry(void* arg)    { reDisplay(); }
void WiFiSetupScene::onStateChange(state_t){ reDisplay(); }

void WiFiSetupScene::switchModeAndRestart() {
    wifi_set_uart_mode(!wifi_use_uart_mode());
    ESP.restart();
}

void WiFiSetupScene::onModeSwitchButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        switchModeAndRestart();
    }
}

void WiFiSetupScene::onRedButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        activate_scene(&menuScene);
    }
}

void WiFiSetupScene::onGreenButtonPress() {
    if (!wifi_in_ap_mode() && !wifi_use_uart_mode()) {
        // WiFi mode: launch AP for credential entry
        wifi_start_ap_setup();
        reDisplay();
    } else {
        ESP.restart();
    }
}

void WiFiSetupScene::onDialButtonPress() {
    activate_scene(&displaySettingsScene);
}

void WiFiSetupScene::onTouchClick() {
    modeSwitchBtn.handleTouch(touchX, touchY);
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    // ── Status badge ──────────────────────────────────────────────────────────
    drawOutlinedRect(BX - 5, BY - 3, BW + 10, BH + 6, 0x8400, 0x8400);  // dark orange
    centered_text("AP Setup Mode", BY + BH / 2 + 3, WHITE, SMALL);

    // ── AP Info ────────────────────────────────────────────────────────────────
    int y = CARD_Y0 + 14;
    int line_height = 24;

    // SSID section
    centered_text("Connect to SSID:", y, LIGHTGREY, TINY);
    y += line_height;
    centered_text(wifi_ap_ssid(), y, CYAN, SMALL);

    y += line_height;
    drawRect(40, y - 2, 160, 1, 0, DARKGREY);  // divider
    
    // IP section
    y += line_height + 8;
    centered_text("Open Browser To:", y, LIGHTGREY, TINY);
    y += line_height;
    centered_text("192.168.4.1", y, GREEN, SMALL);
    
    // ── Button legends ────────────────────────────────────────────────────────
    drawButtonLegends("Stop AP", "Restart", "Exit");
}

void WiFiSetupScene::drawSettingsView() {
    bool       uart_mode = wifi_use_uart_mode();
    WiFiConfig cfg       = wifi_load_config();

    // ── Status badge ──────────────────────────────────────────────────────────
    int        badge_fill;
    int        badge_text = BLACK;
    const char* badge_label;

    if (uart_mode) {
        badge_fill  = BLUE;
        badge_label = "UART Mode";
        badge_text  = WHITE;
    } else if (!cfg.valid) {
        badge_fill  = RED;
        badge_label = "Not Configured";
        badge_text  = WHITE;
    } else {
        bool ws_ok  = websocket_is_connected();
        bool wf_ok  = wifi_is_connected();
        badge_fill  = ws_ok ? GREEN : wf_ok ? YELLOW : RED;
        badge_label = wifi_status_str();
    }
    
    // Draw badge with padding
    drawOutlinedRect(BX - 5, BY - 3, BW + 10, BH + 6, badge_fill, badge_fill);
    centered_text(badge_label, BY + BH / 2 + 3, badge_text, SMALL);

    // ── Info section ──────────────────────────────────────────────────────────
    int y = CARD_Y0 + 10;
    int line_height = 16;

    if (uart_mode) {
        // UART info
        y += line_height;
        drawRect(40, y - 2, 160, 1, 0, DARKGREY);  // divider
        y += 12;
        centered_text("1 Mbaud", y, CYAN, SMALL);
        y += line_height;
        centered_text("Wired UART", y, WHITE, TINY);
    } else if (cfg.valid) {
        // WiFi info
        centered_text("Connected Network", y, LIGHTGREY, TINY);
        y += line_height;
        drawRect(40, y - 2, 160, 1, 0, DARKGREY);  // divider
        y += 12;
        centered_text(cfg.ssid, y, WHITE, SMALL);
        y += line_height;
        centered_text(cfg.fluidnc_ip, y, LIGHTGREY, TINY);
        
        // Signal strength with visual indicator
        y += 16;
        int bars = wifi_signal_bars();
        const char* bars_str = "○○○○○";
        int bar_color = RED;
        if (bars >= 4) { bars_str = "●●●●●"; bar_color = GREEN; }
        else if (bars == 3) { bars_str = "●●●○○"; bar_color = YELLOW; }
        else if (bars == 2) { bars_str = "●●○○○"; bar_color = YELLOW; }
        else if (bars == 1) { bars_str = "●○○○○"; bar_color = RED; }
        
        centered_text(bars_str, y, bar_color, SMALL);
    } else {
        // No config
        y += line_height;
        centered_text("Press Green button", y, ORANGE, TINY);
        y += line_height;
        centered_text("to setup WiFi", y, ORANGE, TINY);
    }

    // ── Mode-switch button ────────────────────────────────────────────────────
    {
        const char* sw_label = uart_mode ? "Switch to WiFi" : "Switch to UART";
        int         sw_color = uart_mode ? GREEN : BLUE;
        
        modeSwitchBtn.set(SBX, SBY, SBW, SBH, sw_label, NAVY, sw_color, sw_color,
            [this]() { onModeSwitchButtonPress(); });
    }

    // ── Button legends ────────────────────────────────────────────────────────
    const char* red_label   = "Back";
    const char* green_label = uart_mode ? "Restart" : "Setup";
    drawButtonLegends(red_label, green_label, "Display");
}

void WiFiSetupScene::reDisplay() {
    background();
    centered_text("Connection Settings", 12);
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    if (wifi_in_ap_mode()) {
        drawApView();
    } else {
        drawSettingsView();
    }

    drawError();
    refreshDisplay();
}

WiFiSetupScene wifiSetupScene;

#endif  // ARDUINO
