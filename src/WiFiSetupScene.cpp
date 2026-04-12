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

extern Scene     menuScene;
extern const char* git_info;

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BX = 50,  BY  = 28,  BW  = 140, BH  = 34;  // status badge
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

void WiFiSetupScene::onRedButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        switchModeAndRestart();
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
    activate_scene(&menuScene);
}

void WiFiSetupScene::onTouchClick() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        switchModeAndRestart();
    }
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    drawOutlinedRect(BX, BY, BW, BH, 0x8400 /* dark orange */, WHITE);
    centered_text("AP Setup Mode", BY + BH / 2 + 3, WHITE, SMALL);

    int y = CARD_Y0;
    drawCard(y,     "Connect to", wifi_ap_ssid(), CYAN);  y += CARD_PITCH;
    drawCard(y,     "Browse to",  "192.168.4.1",  GREEN); y += CH + 14;

    centered_text("Fill in WiFi + FluidNC IP,", y,      LIGHTGREY, TINY);
    centered_text("then tap Save.",              y + 16, LIGHTGREY, TINY);

    drawButtonLegends("Stop AP", "Restart", "Menu");
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
    drawOutlinedRect(BX, BY, BW, BH, badge_fill, WHITE);
    centered_text(badge_label, BY + BH / 2 + 3, badge_text, SMALL);

    // ── Info cards ────────────────────────────────────────────────────────────
    int y = CARD_Y0;

    if (uart_mode) {
        drawCard(y, "Baud Rate",  "1 Mbaud", CYAN);  y += CARD_PITCH;
        drawCard(y, "Transport",  "Wired UART");
    } else if (cfg.valid) {
        drawCardAuto(y, "Network",  cfg.ssid);           y += CARD_PITCH;
        drawCard(y,     "FluidNC",  cfg.fluidnc_ip);     y += CARD_PITCH;

        char sigbuf[24];
        int  bars = wifi_signal_bars();
        snprintf(sigbuf, sizeof(sigbuf), "%s (%d/4)", signal_str(bars), bars);
        drawCard(y, "Signal", sigbuf);
    } else {
        // WiFi mode but no credentials stored yet
        centered_text("No WiFi credentials saved.", CARD_Y0 + 12, LIGHTGREY, SMALL);
        centered_text("Press Green to run AP Setup", CARD_Y0 + 38, LIGHTGREY, TINY);
        centered_text("and configure your network.", CARD_Y0 + 54, LIGHTGREY, TINY);
    }

    // ── Mode-switch button ────────────────────────────────────────────────────
    {
        const char* sw_label = uart_mode ? "Switch to WiFi" : "Switch to UART";
        int         sw_color = uart_mode ? GREEN : BLUE;
        drawOutlinedRect(SBX, SBY, SBW, SBH, NAVY, sw_color);
        centered_text(sw_label, SBY + SBH / 2 + 3, sw_color, SMALL);
    }

    // ── Button legends ────────────────────────────────────────────────────────
    const char* red_label   = uart_mode ? "Use WiFi"  : "Use UART";
    const char* green_label = uart_mode ? "Restart"   : "AP Setup";
    drawButtonLegends(red_label, green_label, "Menu");
}

void WiFiSetupScene::reDisplay() {
    background();
    drawMenuTitle(name());
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
