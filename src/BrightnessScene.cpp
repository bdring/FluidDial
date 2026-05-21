// 2026 - Figamore

#include "BrightnessScene.h"
#include "Drawing.h"
#include "System.h"
#include "SystemScene.h"

static constexpr int MIN_BRIGHTNESS_PCT = 3;  // ~8/255

void BrightnessScene::onEntry(void* arg) {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
        _brightness = std::min(100, std::max(MIN_BRIGHTNESS_PCT, _brightness));
    }
    reDisplay();
}

void BrightnessScene::onEncoder(int delta) {
    if (delta > 0 && _brightness < 100) {
        _brightness = std::min(100, _brightness + 1);
        display.setBrightness(getBrightness());
        setPref("brightness", _brightness);
    }
    if (delta < 0 && _brightness > MIN_BRIGHTNESS_PCT) {
        _brightness = std::max(MIN_BRIGHTNESS_PCT, _brightness - 1);
        display.setBrightness(getBrightness());
        setPref("brightness", _brightness);
    }
    reDisplay();
}

void BrightnessScene::onDialButtonPress() { activate_scene(&systemScene); }
void BrightnessScene::onRedButtonPress()  { activate_scene(&systemScene); }

void BrightnessScene::reDisplay() {
    background();
    centered_text("Brightness", 40, WHITE, SMALL);
    drawRect(45, 52, 150, 1, 0, DARKGREY);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", _brightness);
    centered_text(buf, 100, WHITE, SMALL);

    centered_text("Turn dial to adjust", 140, LIGHTGREY, TINY);

    drawButtonLegends("Back", "", "");
    refreshDisplay();
}

int BrightnessScene::getBrightness() {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
        _brightness = std::min(100, std::max(MIN_BRIGHTNESS_PCT, _brightness));
    }
    return (_brightness * 255) / 100;
}

BrightnessScene brightnessScene;
