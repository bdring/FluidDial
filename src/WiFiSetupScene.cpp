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

// Card geometry shared by connected- and AP-views.
static constexpr int CX = 15;   // card left edge
static constexpr int CW = 210;  // card widt
static constexpr int CH = 26;   // card height
static constexpr int CI = 7;    // card text inset from each side

// Badge geometry (status pill at top of content area).
static constexpr int BX = 50;   // badge left edge
static constexpr int BW = 140;  // badge width
static constexpr int BH = 34;   // badge height
static constexpr int BY = 28;   // badge top

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void drawCard(int y, const char* label, const char* value,
                     int value_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI,      mid, DARKGREY, TINY,  middle_left);
    text(value, CX + CW - CI, mid, value_color, SMALL, middle_right);
}

static void drawCardAuto(int y, const char* label, const char* value,
                         int value_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI, mid, DARKGREY, TINY, middle_left);
    // Auto-fit so long SSIDs shrink instead of overflowing.
    static constexpr int VALUE_W = 130;
    auto_text(std::string(value), CX + CW - CI, mid, VALUE_W,
              value_color, SMALL, middle_right);
}

// ─── Event handlers ───────────────────────────────────────────────────────────

void WiFiSetupScene::onEntry(void* arg) {
    reDisplay();
}

void WiFiSetupScene::onStateChange(state_t) {
    reDisplay();
}

void WiFiSetupScene::onRedButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        wifi_start_ap_setup();
        reDisplay();
    }
}

void WiFiSetupScene::onGreenButtonPress() {
    ESP.restart();
}

void WiFiSetupScene::onDialButtonPress() {
    activate_scene(&menuScene);
}

void WiFiSetupScene::onTouchClick() {
    onRedButtonPress();
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    // ── AP status badge ────────────────────────────────────────────────────────
    drawOutlinedRect(BX, BY, BW, BH, 0x8400 /* dark orange */, WHITE);
    centered_text("AP Mode Active", BY + BH / 2 + 3, WHITE, SMALL);

    // ── Info cards ────────────────────────────────────────────────────────────
    int y = 70;
    drawCard(y, "Connect to", wifi_ap_ssid(), CYAN);
    y += CH + 4;
    drawCard(y, "Then open", "192.168.4.1", GREEN);
    y += CH + 14;

    // ── Instructions ─────────────────────────────────────────────────────────
    centered_text("Browse to the IP above,", y, LIGHTGREY, TINY);
    y += 16;
    centered_text("fill in WiFi + FluidNC IP,", y, LIGHTGREY, TINY);
    y += 16;
    centered_text("then press Save.", y, LIGHTGREY, TINY);

    drawButtonLegends("Stop AP", "Restart", "Menu");
}

void WiFiSetupScene::drawConnectedView() {
    WiFiConfig cfg = wifi_load_config();

    if (!cfg.valid) {
        // ── Not configured ─────────────────────────────────────────────────────
        drawOutlinedRect(BX, BY, BW, BH, RED, WHITE);
        centered_text("Not Configured", BY + BH / 2 + 3, WHITE, SMALL);

        centered_text("Press Red or touch to", 112, LIGHTGREY, TINY);
        centered_text("start WiFi setup.", 130, LIGHTGREY, TINY);

        drawButtonLegends("AP Setup", "Restart", "Menu");
        return;
    }

    // ── Status badge ──────────────────────────────────────────────────────────
    bool ws_ok       = websocket_is_connected();
    bool wf_ok       = wifi_is_connected();
    int  badge_fill  = ws_ok ? GREEN : wf_ok ? YELLOW : RED;
    int  badge_text  = BLACK;

    drawOutlinedRect(BX, BY, BW, BH, badge_fill, WHITE);
    centered_text(wifi_status_str(), BY + BH / 2 + 3, badge_text, SMALL);

    // ── Info cards ────────────────────────────────────────────────────────────
    int y = 70;
    drawCardAuto(y, "Network", cfg.ssid);
    y += CH + 4;
    drawCard(y, "FluidNC IP", cfg.fluidnc_ip);
    y += CH + 6;

    // ── Machine state badge (only when WebSocket is live) ─────────────────────
    if (ws_ok) {
        drawStatusTiny(y);
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    std::string ver = "Ver ";
    ver += git_info;
    centered_text(ver.c_str(), 182, DARKGREY, TINY);

    drawButtonLegends("AP Setup", "Restart", "Menu");
}

void WiFiSetupScene::reDisplay() {
    background();
    drawMenuTitle(name());

    // divider
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    if (wifi_in_ap_mode()) {
        drawApView();
    } else {
        drawConnectedView();
    }

    drawError();
    refreshDisplay();
}

WiFiSetupScene wifiSetupScene;

#endif  // ARDUINO
