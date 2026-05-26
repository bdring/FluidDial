// 2026 - Figamore

#ifdef USE_WIFI

#include "WiFiSetupScene.h"
#include "WiFiConnection.h"
#include "PeerLink.h"
#include "ESPNowPairingScene.h"
#include "TransportScene.h"
#include "Drawing.h"
#include "Menu.h"
#include "System.h"
#include "Button.h"
#include "SystemScene.h"

extern Scene       menuScene;
extern const char* git_info;

// ─── Geometry ─────────────────────────────────────────────────────────────────

static constexpr int BX = 20, BY = 28, BW = 200, BH = 34;       // status badge
static constexpr int CX = 15, CW = 210, CH = 28, CI = 8;        // info cards
static constexpr int SBX = 12, SBY = 160, SBW = 216, SBH = 36;  // switch button

static constexpr int CARD_Y0    = 68;
static constexpr int CARD_PITCH = CH + 4;  // 32 px per card row

// ─── Card drawing helpers ──────────────────────────────────────────────────────

static void drawCard(int y, const char* label, const char* value, int val_color = WHITE) {
    drawOutlinedRect(CX, y, CW, CH, NAVY, WHITE);
    int mid = y + CH / 2 + 2;
    text(label, CX + CI, mid, DARKGREY, TINY, middle_left);
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

void WiFiSetupScene::onModeSwitchButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap_and_restart();
    } else {
        push_scene(&transportScene);
    }
}

void WiFiSetupScene::onRedButtonPress() {
    if (wifi_in_ap_mode()) {
        wifi_stop_ap();
        reDisplay();
    } else {
        activate_scene(&menuScene);
    }
}

void WiFiSetupScene::onGreenButtonPress() {
    if (wifi_in_ap_mode()) {
#ifdef ARDUINO
        ESP.restart();
#endif
    } else if (wifi_use_espnow_mode()) {
        push_scene(&espnowPairingScene);
    } else if (!wifi_use_uart_mode()) {
        wifi_start_ap_setup();
        reDisplay();
    } else {
#ifdef ARDUINO
        ESP.restart();
#endif
    }
}

void WiFiSetupScene::onDialButtonPress() {
    if (!wifi_in_ap_mode()) {
        activate_scene(&systemScene);
    }
}

void WiFiSetupScene::onTouchClick() {
    modeSwitchBtn.handleTouch(touchX, touchY);
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void WiFiSetupScene::drawApView() {
    // ── Status badge ──────────────────────────────────────────────────────────
    int by = round_display ? BY + 5 : BY - 3;
    int bx = round_display ? 55 : BX;
    int bw = round_display ? 130 : BW;
    int bh = round_display ? 24 : BH;
    int ty = round_display ? BY + bh / 2 + 10 : BY + bh / 2 + 3;
    drawOutlinedRect(bx - 5, by, bw + 10, bh + 6, 0x8400, 0x8400);  // dark orange
    centered_text("AP Mode", ty, WHITE, SMALL);

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
    y += line_height + 4;
    centered_text("Open Browser To:", y, LIGHTGREY, TINY);
    y += line_height;
    centered_text("192.168.4.1", y, GREEN, SMALL);

    // ── Button legends ────────────────────────────────────────────────────────
    drawButtonLegends("Exit", "Restart", "");
}

void WiFiSetupScene::drawSettingsView() {
    TransportMode transport  = wifi_get_transport();
    bool          uart_mode  = (transport == TransportMode::UART);
    bool          espnow_mode = (transport == TransportMode::ESPNOW);
    WiFiConfig    cfg        = wifi_active_config();
    bool          ws_ok      = websocket_is_connected();
    bool          wf_ok      = wifi_is_connected();

    // ── Status badge ──────────────────────────────────────────────────────────
    int         badge_fill;
    int         badge_outline;
    int         badge_text;
    const char* badge_label;

    if (uart_mode) {
        badge_fill    = 0x001a4d;
        badge_outline = 0x4da6ff;
        badge_label   = "UART Mode";
        badge_text    = 0x4da6ff;
    } else if (espnow_mode) {
        if (espnow_is_connected()) {
            badge_fill    = 0x003300;
            badge_outline = 0xcc66ff;
            badge_label   = "Using ESP-NOW";
            badge_text    = 0xcc66ff;
        } else if (espnow_is_reconnecting()) {
            static const char* rc_frames[] = {"Searching", "Searching.", "Searching..", "Searching..."};
            badge_fill    = 0x1a0033;
            badge_outline = 0xcc66ff;
            badge_label   = rc_frames[(millis() / 400) % 4];
            badge_text    = 0xcc66ff;
        } else if (espnow_is_paired()) {
            badge_fill    = 0x1a001a;
            badge_outline = 0xcc66ff;
            badge_label   = "ESP-NOW Paired";
            badge_text    = 0xcc66ff;
        } else {
            badge_fill    = 0x1a001a;
            badge_outline = 0xcc66ff;
            badge_label   = "ESP-NOW";
            badge_text    = 0xcc66ff;
        }
    } else if (!cfg.valid) {
        badge_fill    = 0x4d0000;
        badge_outline = 0xe02b2b;
        badge_label   = "Not Configured";
        badge_text    = 0xe02b2b;
    } else if (ws_ok) {
        badge_fill    = 0x003300;
        badge_outline = 0x66ff66;
        badge_label   = "Ready";
        badge_text    = 0x66ff66;
    } else if (wf_ok) {
        static const char* nc_frames[] = {"FluidNC", "FluidNC.", "FluidNC..", "FluidNC..."};
        badge_fill    = 0x332200;
        badge_outline = YELLOW;
        badge_label   = nc_frames[(millis() / 400) % 4];
        badge_text    = YELLOW;
    } else if (wifi_last_error()) {
        badge_fill    = 0x4d0000;
        badge_outline = RED;
        badge_label   = "WiFi Error";
        badge_text    = WHITE;
    } else {
        static const char* wifi_frames[] = {"WiFi", "WiFi.", "WiFi..", "WiFi..."};
        badge_fill    = 0x2a0000;
        badge_outline = 0xe02b2b;
        badge_label   = wifi_frames[(millis() / 400) % 4];
        badge_text    = WHITE;
    }

    int bx = round_display ? 55 : BX;
    int by = round_display ? BY + 6 : BY - 3;
    int bw = round_display ? 130 : BW;
    int bh = round_display ? 24 : BH;
    int ty = round_display ? BY + bh / 2 + 9 : BY + bh / 2 + 3;
    drawOutlinedRect(bx - 5, by, bw + 10, bh + 6, badge_fill, badge_outline);
    centered_text(badge_label, ty, badge_text, round_display ? TINY : SMALL);

    // ── Info section ──────────────────────────────────────────────────────────
    int y = CARD_Y0;

    if (uart_mode) {
        y += 28;
        centered_text("1 Mbaud", y, 0xe02b2b, SMALL);
        y += 20;
        centered_text("Wired UART", y, WHITE, TINY);
    } else if (espnow_mode) {
        y += (round_display ? 8 : 14);
        if (!espnow_is_paired()) {
            centered_text("Not yet paired", y, DARKGREY, TINY);
            y += 18;
            centered_text("Press green to pair", y, 0xcc66ff, TINY);
        } else if (espnow_is_reconnecting()) {
            centered_text("Connection lost", y, YELLOW, TINY);
            y += 18;
            centered_text("Scanning...", y, 0xcc66ff, TINY);
        } else {
            y += 18;
            y += (round_display ? 14 : 20);
            y += 14;
            if (espnow_is_connected()) {
                int8_t rssi = espnow_rssi();
                if (rssi != 0) {
                    char rssi_buf[16];
                    snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", rssi);
                    centered_text(rssi_buf, y, 0xcc66ff, SMALL);
                } else {
                    // centered_text(espnow_status_str(), y, 0xcc66ff, SMALL);
                }
            } else {
                centered_text(espnow_status_str(), y, DARKGREY, SMALL);
            }
        }
    } else if (!cfg.valid) {
        y += 14;
        centered_text("Press green button", y, 0xe02b2b, TINY);
        y += 18;
        centered_text("to setup WiFi", y, 0xe02b2b, TINY);
    } else {
        y += (round_display ? 8 : 10);
        centered_text("Network", y, DARKGREY, TINY);
        y += 20;
        centered_text(cfg.ssid, y, WHITE, SMALL);
        y += (round_display ? 14 : 20);
        drawRect(40, y, 160, 1, 0, DARKGREY);
        y += 14;

        if (wifi_last_error()) {
            centered_text("WiFi Error", y, DARKGREY, TINY);
            y += 20;
            centered_text(wifi_last_error(), y, RED, SMALL);
            y += 20;
            centered_text("Retrying...", y, 0x888888, TINY);
        } else {
            centered_text("FluidNC Address", y, DARKGREY, TINY);
            y += 20;
            int ip_color = ws_ok ? GREEN : wf_ok ? YELLOW : LIGHTGREY;
            centered_text(cfg.fluidnc_ip, y, ip_color, SMALL);
            y += 22;
        }
    }

    {
        int sbx = round_display ? 40 : SBX;
        int sbw = round_display ? 160 : SBW;
        int sby = round_display ? SBY : SBY + 6;
        modeSwitchBtn.font = round_display ? TINY : SMALL;
        modeSwitchBtn.set(sbx, sby, sbw, SBH, "Switch Mode",
                          0x001a4d, 0x4da6ff, 0x4da6ff,
                          [this]() { onModeSwitchButtonPress(); });
    }

    // ── Button legends ────────────────────────────────────────────────────────
    const char* green_label;
    if (uart_mode)   green_label = "Restart";
    else if (espnow_mode) green_label = espnow_is_paired() ? "Re-pair" : "Pair";
    else             green_label = "Setup";
    drawButtonLegends("Back", green_label, "More");
}

void WiFiSetupScene::reDisplay() {
    background();

    const char* title;
    if (wifi_use_espnow_mode()) {
        title = round_display ? "ESP-NOW" : "Connection Settings";
    } else if (round_display) {
        if (wifi_in_ap_mode() || wifi_use_uart_mode() || !wifi_active_config().valid) {
            title = "WiFi Setup";
        } else if (websocket_is_connected()) {
            title = "Connected";
        } else if (wifi_is_connected()) {
            title = "Connecting";
        } else {
            title = "WiFi Setup";
        }
    } else {
        if (wifi_in_ap_mode() || wifi_use_uart_mode() || !wifi_active_config().valid) {
            title = "Connection Settings";
        } else if (websocket_is_connected()) {
            title = " Connected to FluidNC";
        } else if (wifi_is_connected()) {
            title = " Connecting to FluidNC";
        } else {
            title = "Connecting to WiFi";
        }
    }
    if (round_display) {
        centered_text(title, 18);
        drawRect(70, 28, 100, 1, 0, DARKGREY);
    } else {
        centered_text(title, 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    if (wifi_in_ap_mode()) {
        drawApView();
    } else {
        drawSettingsView();
    }

    drawError();
    refreshDisplay();
}

WiFiSetupScene wifiSetupScene;

#endif  // USE_WIFI
