// 2026 - Figamore

#ifdef USE_M5

#include "BrightnessScene.h"
#include "Drawing.h"
#include "System.h"
#include "SystemScene.h"

static constexpr int MIN_BRIGHTNESS = 8;

void BrightnessScene::onEntry(void* arg) {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
    }
    reDisplay();
}

void BrightnessScene::onEncoder(int delta) {
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

void BrightnessScene::onDialButtonPress() { activate_scene(&systemScene); }
void BrightnessScene::onRedButtonPress()  { activate_scene(&systemScene); }

void BrightnessScene::reDisplay() {
    background();
    centered_text("Brightness", 40, WHITE, SMALL);
    drawRect(45, 52, 150, 1, 0, DARKGREY);

    int  pct = (_brightness * 100) / 255;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    centered_text(buf, 100, WHITE, SMALL);

    centered_text("Turn dial to adjust", 140, LIGHTGREY, TINY);

    drawButtonLegends("Back", "", "");
    refreshDisplay();
}

int BrightnessScene::getBrightness() {
    if (initPrefs()) {
        getPref("brightness", &_brightness);
    }
    return _brightness;
}

BrightnessScene brightnessScene;

#endif  // USE_M5
