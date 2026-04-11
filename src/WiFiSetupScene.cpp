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
    // Same toggle as red button — convenient for single-button displays.
    onRedButtonPress();
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    const int cx = 120;   // horizontal centre for centred text
    int y = 60;

    centered_text("AP Mode Active", y, YELLOW, SMALL);
    y += 26;

    centered_text("Connect your device to:", y, LIGHTGREY, TINY);
    y += 20;
    centered_text(wifi_ap_ssid(), y, WHITE, SMALL);
    y += 26;

    centered_text("Then open a browser at:", y, LIGHTGREY, TINY);
    y += 20;
    centered_text("192.168.4.1", y, GREEN, SMALL);
    y += 26;

    centered_text("Fill in the form to", y, LIGHTGREY, TINY);
    y += 18;
    centered_text("configure WiFi + FluidNC", y, LIGHTGREY, TINY);

    drawButtonLegends("Stop AP", "Restart", "Menu");
}

void WiFiSetupScene::drawConnectedView() {
    WiFiConfig cfg  = wifi_load_config();
    int        y    = 58;

    if (!cfg.valid) {
        centered_text("Not Configured", y, RED, SMALL);
        y += 28;
        centered_text("Press red button or", y, LIGHTGREY, TINY);
        y += 18;
        centered_text("touch to start WiFi setup", y, LIGHTGREY, TINY);
    } else {
        // Status line with colour-coded indicator.
        color_t sc = websocket_is_connected() ? GREEN
                   : wifi_is_connected()       ? YELLOW
                                               : RED;
        centered_text(wifi_status_str(), y, sc, SMALL);
        y += 26;

        // SSID.
        centered_text(cfg.ssid, y, LIGHTGREY, TINY);
        y += 20;

        // FluidNC IP.
        std::string ip_str = "FluidNC: ";
        ip_str += cfg.fluidnc_ip;
        centered_text(ip_str.c_str(), y, LIGHTGREY, TINY);
        y += 26;

        // Version.
        std::string ver_str = "Ver ";
        ver_str += git_info;
        centered_text(ver_str.c_str(), y, DARKGREY, TINY);
        y += 22;

        centered_text("Red btn / touch: AP setup", y, DARKGREY, TINY);
    }

    drawButtonLegends("AP Setup", "Restart", "Menu");
}

void WiFiSetupScene::reDisplay() {
    background();
    drawStatus();
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
