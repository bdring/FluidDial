// 2026 - Figamore
// DisplaySettingsScene.cpp — lets the user cycle through CYD screen orientations.
//
// Turn the encoder to step through all available layouts (rotation × button position).

#ifdef USE_WIFI

#include "DisplaySettingsScene.h"
#include "Drawing.h"
#include "System.h"
#include "SystemScene.h"

static const char* layout_names[] = {
    "0 deg - Btns Bottom",   // rotation 0, buttons below
    "0 deg - Btns Top",      // rotation 0, buttons above
    "90 deg - Btns Right",    // rotation 1, buttons right
    "90 deg - Btns Left",     // rotation 1, buttons left
    "180 deg - Btns Bottom",  // rotation 2, buttons below
    "180 deg - Btns Top",     // rotation 2, buttons above
    "270 deg - Btns Left",    // rotation 3, buttons left
    "270 deg - Btns Right",   // rotation 3, buttons right
};
static const int n_layout_names = sizeof(layout_names) / sizeof(layout_names[0]);

void DisplaySettingsScene::onEntry(void* arg) {
    reDisplay();
}

void DisplaySettingsScene::onEncoder(int delta) {
    next_layout(delta);
    reDisplay();
}

void DisplaySettingsScene::onDialButtonPress() {
    activate_scene(&systemScene);
}

void DisplaySettingsScene::onRedButtonPress() {
    activate_scene(&systemScene);
}

void DisplaySettingsScene::reDisplay() {
    background();
    drawMenuTitle("Display");
    drawRect(55, 22, 130, 1, 0, DARKGREY);

    char idx_buf[12];
    snprintf(idx_buf, sizeof(idx_buf), "%d / %d", layout_num + 1, num_layouts);
    centered_text(idx_buf, 70, DARKGREY, TINY);

    // Layout name
    const char* name = (layout_num >= 0 && layout_num < n_layout_names)
                           ? layout_names[layout_num]
                           : "Unknown";
    centered_text(name, 100, WHITE, SMALL);

    centered_text("Turn dial to rotate", 140, LIGHTGREY, TINY);

    drawButtonLegends("Back", "", "");
    refreshDisplay();
}

DisplaySettingsScene displaySettingsScene;

#endif  // USE_WIFI
