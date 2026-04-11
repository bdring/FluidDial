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

extern Scene menuScene;
extern const char* git_info;

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
    int y = 32;

    centered_text("AP Mode Active", y, YELLOW, SMALL);
    y += 28;

    centered_text("Connect your device to:", y, DARKGREY, TINY);
    y += 18;
    centered_text(wifi_ap_ssid(), y, WHITE, SMALL);
    y += 28;

    centered_text("Then open a browser at:", y, DARKGREY, TINY);
    y += 18;
    centered_text("192.168.4.1", y, GREEN, SMALL);
    y += 28;

    centered_text("Fill in WiFi SSID,", y, LIGHTGREY, TINY);
    y += 16;
    centered_text("password + FluidNC IP", y, LIGHTGREY, TINY);
    y += 16;
    centered_text("then press Save", y, LIGHTGREY, TINY);

    drawButtonLegends("Stop AP", "Restart", "Menu");
}

void WiFiSetupScene::drawConnectedView() {
    WiFiConfig cfg = wifi_load_config();

    if (!cfg.valid) {
        centered_text("Not Configured", 90, RED, SMALL);
        centered_text("Press red button or", 120, LIGHTGREY, TINY);
        centered_text("touch to start WiFi setup", 138, LIGHTGREY, TINY);
        drawButtonLegends("AP Setup", "Restart", "Menu");
        return;
    }

    // ── Status badge ──────────────────────────────────────────────────────────
    bool ws_ok       = websocket_is_connected();
    bool wf_ok       = wifi_is_connected();
    int  badge_color = ws_ok ? GREEN : wf_ok ? YELLOW : RED;

    // Filled rounded-rect badge with the connection status centred inside.
    static constexpr int BW = 140, BH = 30, BY = 30;
    drawRect((240 - BW) / 2, BY, BW, BH, 8, badge_color);
    centered_text(wifi_status_str(), BY + BH / 2 + 3, BLACK, SMALL);

    // ── Info rows ─────────────────────────────────────────────────────────────
    // Each row: small DARKGREY label above a larger WHITE value.
    centered_text("Network", 72, DARKGREY, TINY);
    centered_text(cfg.ssid, 88, WHITE, SMALL);

    centered_text("FluidNC IP", 112, DARKGREY, TINY);
    centered_text(cfg.fluidnc_ip, 128, WHITE, SMALL);

    // ── Machine state badge (only when WebSocket is live) ─────────────────────
    if (ws_ok) {
        drawStatusTiny(150);
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    std::string ver_str = "Ver ";
    ver_str += git_info;
    centered_text(ver_str.c_str(), 182, DARKGREY, TINY);

    drawButtonLegends("AP Setup", "Restart", "Menu");
}

void WiFiSetupScene::reDisplay() {
    background();
    drawMenuTitle(name());

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
