// 2026 - Figamore
// SystemScene.cpp — "More" settings hub: display orientation, restart, sleep.

#ifdef USE_WIFI

#include "SystemScene.h"
#include "DisplaySettingsScene.h"
#include "BrightnessScene.h"
#include "WiFiSetupScene.h"
#include "Drawing.h"
#include "System.h"
#include "FluidNCModel.h"

struct SysItem {
    const char* label;
    const char* sublabel;
};

static const SysItem items[] = {
    { "Restart",  ""                  },
#ifdef USE_M5
    { "Sleep",    "Press red to wake" },
    { "Brightness", "Turn to adjust"  },
#else
    { "Display",  "Adjust orientation" },
#endif
};
static const int N_ITEMS = (int)(sizeof(items) / sizeof(items[0]));

static constexpr int ITEM_H_ROUND     = 48;
static constexpr int ITEM_PITCH_ROUND = 48;
static constexpr int ITEM_H_CYD       = 48;
static constexpr int ITEM_PITCH_CYD   = 72;
static constexpr int START_Y_ROUND    = 50;
static constexpr int START_Y_CYD      = 60;

int SystemScene::itemCount() { return N_ITEMS; }

void SystemScene::onEntry(void* arg) {
    _selected = 0;
    reDisplay();
}

void SystemScene::onEncoder(int delta) {
    _selected += delta;
    if (_selected < 0)        _selected = N_ITEMS - 1;
    if (_selected >= N_ITEMS) _selected = 0;
    reDisplay();
}

void SystemScene::activateSelected() {
    switch (_selected) {
        case 0:
#ifdef ARDUINO
            esp_restart();
#endif
            break;
        case 1:
#ifdef USE_M5
            set_disconnected_state();
#    ifdef ARDUINO
            background();
            centered_text("Red button to wake", 118, RED, TINY);
            refreshDisplay();
            delay_ms(2000);
            deep_sleep(0);
#    endif
#else
            activate_scene(&displaySettingsScene);
#endif
            break;
#ifdef USE_M5
        case 2:
            activate_scene(&brightnessScene);
            break;
#endif
    }
}

void SystemScene::onDialButtonPress()  { activateSelected(); }
void SystemScene::onGreenButtonPress() { activateSelected(); }
void SystemScene::onRedButtonPress()   { activate_scene(&wifiSetupScene); }

void SystemScene::onTouchClick() {
    int item_h    = round_display ? ITEM_H_ROUND    : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y   = round_display ? START_Y_ROUND   : START_Y_CYD;
    for (int i = 0; i < N_ITEMS; i++) {
        int y = start_y + i * item_pitch;
        if (touchX >= 30 && touchX <= 210 && touchY >= y - 2 && touchY < y + item_h - 4) {
            _selected = i;
            reDisplay();
            activateSelected();
            return;
        }
    }
}

void SystemScene::reDisplay() {
    int item_h     = round_display ? ITEM_H_ROUND    : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y    = round_display ? START_Y_ROUND   : START_Y_CYD;

    background();
    drawMenuTitle("System");
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    for (int i = 0; i < N_ITEMS; i++) {
        int  y   = start_y + i * item_pitch;
        bool sel = (i == _selected);

        if (sel) {
            drawOutlinedRect(30, y - 2, 180, item_h - 4, 0x001a4d, 0x4da6ff);
        }

        bool has_sub = items[i].sublabel[0] != '\0';
        int  label_y = has_sub ? y + 12 : y + item_h / 2;
        centered_text(items[i].label,    label_y, sel ? WHITE    : LIGHTGREY, SMALL);
        if (has_sub) {
            centered_text(items[i].sublabel, y + 32, sel ? 0x4da6ff : DARKGREY, TINY);
        }
    }

#if defined(USE_LOVYANGFX) && defined(CYD_BATTERY_ADC)
    if (!round_display) {
        int mv = battery_millivolts();
        if (mv > 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Battery: %d.%02d V", mv / 1000, (mv % 1000) / 10);
            centered_text(buf, 210, LIGHTGREY, TINY);
        }
    }
#endif

    drawButtonLegends("Back", "Select", "");
    refreshDisplay();
}

SystemScene systemScene;

#endif  // USE_WIFI
