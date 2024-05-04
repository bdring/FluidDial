// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"

class PowerScene : public Scene {
private:
    int _brightness = 255;

public:
    PowerScene() : Scene("Power") {}
    void onEntry(void* arg) override {
        if (initPrefs()) {
            getPref("brightness", &_brightness);
        }
    }
    void onRedButtonPress() {
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
    void onGreenButtonPress() {
        set_disconnected_state();
#ifdef ARDUINO
        esp_restart();
#endif
    }
    void onDialButtonPress() { pop_scene(); }
    void reDisplay() {
        background();
#ifdef ARDUINO
        const char* greenLegend = "Restart";
#else
        const char* greenLegend = "";
#endif
        text("Brightness:", 122, 90, LIGHTGREY, TINY, bottom_right);
        text(intToCStr(_brightness), 126, 90, GREEN, TINY, bottom_left);
        drawButtonLegends("Sleep", greenLegend, "Back");
        refreshDisplay();
    }

    void onEncoder(int delta) {
        if (delta > 0 && _brightness < 255) {
            display.setBrightness(++_brightness);
            dbg_printf("Bright %d\n", _brightness);
            setPref("brightness", _brightness);
        }
        if (delta < 0 && _brightness > 0) {
            display.setBrightness(--_brightness);
            dbg_printf("Bright %d\n", _brightness);
            setPref("brightness", _brightness);
        }
        reDisplay();
    }
} powerScene;
