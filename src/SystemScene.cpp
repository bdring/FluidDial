// 2026 - Figamore
// SystemScene.cpp — "More" settings hub: display orientation, restart, sleep.

#ifdef USE_WIFI

#include "SystemScene.h"
#include "DisplaySettingsScene.h"
#include "WiFiSetupScene.h"
#include "Drawing.h"
#include "System.h"
#include "FluidNCModel.h"

struct SysItem {
    const char* label;
    const char* sublabel;
};

static const SysItem items[] = {
    { "Display",  "Screen orientation" },
    { "Restart",  "Reboot pendant"     },
#ifdef USE_M5
    { "Sleep",    "Red button to wake" },
#endif
};
static const int N_ITEMS = (int)(sizeof(items) / sizeof(items[0]));

static constexpr int ITEM_H   = 48;
static constexpr int START_Y  = 50;

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
            activate_scene(&displaySettingsScene);
            break;
        case 1:
#ifdef ARDUINO
            esp_restart();
#endif
            break;
#ifdef USE_M5
        case 2:
            set_disconnected_state();
#    ifdef ARDUINO
            centered_text("Red button to wake", 118, RED, TINY);
            refreshDisplay();
            delay_ms(2000);
            deep_sleep(0);
#    endif
            break;
#endif
    }
}

void SystemScene::onDialButtonPress()  { activateSelected(); }
void SystemScene::onGreenButtonPress() { activateSelected(); }
void SystemScene::onRedButtonPress()   { activate_scene(&wifiSetupScene); }

void SystemScene::reDisplay() {
    background();
    drawMenuTitle("System");
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    for (int i = 0; i < N_ITEMS; i++) {
        int  y   = START_Y + i * ITEM_H;
        bool sel = (i == _selected);

        if (sel) {
            drawOutlinedRect(20, y - 2, 200, ITEM_H - 4, 0x001a4d, 0x4da6ff);
        }

        centered_text(items[i].label,    y + 12, sel ? WHITE    : LIGHTGREY, SMALL);
        centered_text(items[i].sublabel, y + 28, sel ? 0x4da6ff : DARKGREY,  TINY);
    }

    drawButtonLegends("Back", "Select", "");
    refreshDisplay();
}

SystemScene systemScene;

#endif  // USE_WIFI
