// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#ifdef ARDUINO
#    include <driver/rtc_io.h>
#endif

#ifdef ARDUINO
// The M5 Library is broken with respect to deep sleep on M5 Dial
// so we have to do it ourselves.  The problem is that the WAKE
// button is supposed to be the dial button that connects to GPIO42,
// but that can't work because GPIO42 is not an RTC GPIO and thus
// cannot be used as an ext0 wakeup source.

// For some reason, GPIO_1 is the only one of the button pins
// that works right for wakeup.  If GPIO_2 is used, the system
// wakes up immediately without a button press.
#    define WAKEUP_GPIO (gpio_num_t) RED_BUTTON_PIN

void deep_sleep(int us) {
    M5.Display.sleep();
    rtc_gpio_pullup_en(WAKEUP_GPIO);
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, false);
    while (digitalRead(WAKEUP_GPIO) == false) {
        delay_ms(10);
    }
    if (us > 0) {
        esp_sleep_enable_timer_wakeup(us);
    } else {
        // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_deep_sleep_start();
}
#endif

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
            M5Dial.Display.setBrightness(++_brightness);
            setPref("brightness", _brightness);
        }
        if (delta < 0 && _brightness > 0) {
            M5Dial.Display.setBrightness(--_brightness);
            setPref("brightness", _brightness);
        }
        reDisplay();
    }
} powerScene;
