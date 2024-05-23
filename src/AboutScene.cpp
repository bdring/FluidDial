// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"

extern Scene menuScene;

extern const char* git_info;  // auto generated version.cpp

class AboutScene : public Scene {
private:
public:
    AboutScene() : Scene("About", 4) {}

    void onEntry(void* arg) {
        send_line("$G");
        send_line("$I");
    }

    void onDialButtonPress() { activate_scene(&menuScene); }
    void onGreenButtonPress() {}
    void onRedButtonPress() {}

    void onTouchClick() override {
        fnc_realtime(StatusReport);
        if (state == Idle) {
            send_line("$G");
            send_line("$I");
        }
    }

    void onEncoder(int delta) {
        next_layout(delta);
        reDisplay();
    }
    void onStateChange(state_t old_state) { reDisplay(); }
    void reDisplay() {
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

        drawMenuTitle(current_scene->name());
        drawButtonLegends("", "", "Menu");
        drawError();  // if there is one
        refreshDisplay();
    }
};
AboutScene aboutScene;
