// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"
#include "AboutScene.h"

extern Scene menuScene;

extern const char* git_info;  // auto generated version.cpp

static const int MIN_BRIGHTNESS = 8;

void AboutScene::onEntry(void* arg) {
    // if (initPrefs()) {
    //     getPref("brightness", &_brightness);
    // }
    getBrightness();
    send_line("$G");
    send_line("$I");
}

void AboutScene::onDialButtonPress() {
    activate_scene(&menuScene);
}
void AboutScene::onGreenButtonPress() {
#ifdef ARDUINO
    esp_restart();
#endif
}
void AboutScene::onRedButtonPress() {
    set_disconnected_state();
#ifdef ARDUINO
    centered_text("Use red button to wakeup", 118, RED, TINY);
    refreshDisplay();
    delay_ms(2000);

    deep_sleep(0);
#else
    dbg_println("Sleep");
#endif
}

void AboutScene::onTouchClick() {
    fnc_realtime(StatusReport);
    if (state == Idle) {
        send_line("$G");
        send_line("$I");
    }
}

void AboutScene::onEncoder(int delta) {
    if (delta > 0 && _brightness < 255) {
        display.setBrightness(++_brightness);
        setPref("brightness", _brightness);
    }
    if (delta < 0 && _brightness > MIN_BRIGHTNESS) {
        display.setBrightness(--_brightness);
        setPref("brightness", _brightness);
    }
    reDisplay();
}
void AboutScene::onStateChange(state_t old_state) {
    reDisplay();
}
void AboutScene::reDisplay() {
    background();
    drawStatus();

    const int key_x     = 118;
    const int val_x     = 122;
    const int y_spacing = 20;
    int       y         = 80;

    std::string version_str = "Ver ";
    version_str += git_info;
    centered_text(version_str.c_str(), y, LIGHTGREY, TINY);
    y += 10;
#ifdef FNC_BAUD  // FNC_BAUD might not be defined for Windows
    text("FNC baud:", key_x, y += y_spacing, LIGHTGREY, TINY, bottom_right);
    text(intToCStr(FNC_BAUD), val_x, y, GREEN, TINY, bottom_left);
#endif

#ifndef DEBUG_TO_USB  // backlight shares a pin with this.
    text("Brightness:", key_x, y += y_spacing, LIGHTGREY, TINY, bottom_right);
    text(intToCStr(_brightness), val_x, y, GREEN, TINY, bottom_left);
#endif

    if (wifi_ssid.length()) {
        std::string wifi_str = wifi_mode;
        if (wifi_mode == "No Wifi") {
            centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
        } else {
            wifi_str += " ";
            wifi_str += wifi_ssid;
            centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
            if (wifi_mode == "STA" && wifi_connected == "Not connected") {
                centered_text(wifi_connected.c_str(), y += y_spacing, RED, TINY);
            } else {
                wifi_str = "IP ";
                wifi_str += wifi_ip;
                centered_text(wifi_str.c_str(), y += y_spacing, LIGHTGREY, TINY);
            }
        }
    }

#ifdef ARDUINO
    const char* greenLegend = "Restart";
#else
    const char* greenLegend = "";
#endif

    //drawOptionButton("Tool Menu", enable_tool_menu, 40, 135, 160);

    drawMenuTitle(current_scene->name());

    drawButtonLegends("Sleep", greenLegend, "Menu");
    drawError();  // if there is one
    refreshDisplay();
}

int AboutScene::getBrightness() {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
    }
    return _brightness;
}

AboutScene aboutScene;
