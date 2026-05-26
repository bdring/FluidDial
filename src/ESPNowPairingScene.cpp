// 2026 - Figamore

#ifdef USE_WIFI

#    include "ESPNowPairingScene.h"
#    include "PeerLink.h"
#    include "WiFiConnection.h"
#    include "Drawing.h"
#    include "System.h"
#    include "Scene.h"
#    include <stdio.h>

extern Scene menuScene;
extern Scene firstBootScene;

int         badge_fill;
int         badge_outline;
int         badge_text;
const char* badge_label;

static void draw_code(const char* code) {
    if (round_display) {
        centered_text(code, 88, WHITE, LARGE);
    } else {
        centered_text(code, 105, WHITE, LARGE);
    }
}

void ESPNowPairingScene::onEntry(void* arg) {
    _fallback = arg ? static_cast<Scene*>(arg) : &firstBootScene;
    set_disconnected_state();
    espnow_start_pairing();
    reDisplay();
}

void ESPNowPairingScene::onExit() {
    if (!espnow_pairing_complete()) {
        espnow_cancel_pairing();
    }
}

void ESPNowPairingScene::onRedButtonPress() {
    espnow_cancel_pairing();
    if (parent_scene()) {
        pop_scene();
    } else {
        activate_at_top_level(_fallback);
    }
}

void ESPNowPairingScene::onPoll() {
    if (espnow_pairing_complete()) {
        activate_scene(&menuScene);
        return;
    }
    reDisplay();
}

void ESPNowPairingScene::reDisplay() {
    background();

    if (!round_display) {
        centered_text("ESP-NOW Pairing", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    const char* code = espnow_pairing_code();

    badge_fill    = 0x001a4d;
    badge_outline = 0x4da6ff;
    badge_label   = "Pairing Code";
    badge_text    = 0x4da6ff;

    static constexpr int BX = 20, BY = 36, BW = 200, BH = 34;

    int bx = round_display ? 55 : BX;
    int by = round_display ? BY - 9 : BY - 3;
    int bw = round_display ? 130 : BW;
    int bh = round_display ? 24 : BH;
    int ty = round_display ? BY - 9 + bh / 2 + 3 : BY + bh / 2 + 3;
    drawOutlinedRect(bx - 5, by, bw + 10, bh + 6, badge_fill, badge_outline);
    centered_text(badge_label, ty, badge_text, round_display ? TINY : SMALL);

    if (round_display) {
        draw_code(code);
        centered_text("Add to config.yaml:", 118, DARKGREY, TINY);

        drawOutlinedRect(BX-8, 132, BW+12, 40, BLACK, badge_outline);
        text("espnow:", 50, 144, 0x4da6ff, TINY);

        char hint[28];
        snprintf(hint, sizeof(hint), "  pair_code: %s", code);
        centered_text(hint, 158, 0x4da6ff, TINY);
    } else {
        draw_code(code);
        centered_text("Add to config.yaml:", 150, DARKGREY, TINY);

        drawOutlinedRect(BX-10, 166, BW+18, 40, BLACK, badge_outline);
        text("espnow:", 50, 176, 0x4da6ff, TINY);

        char hint[28];
        snprintf(hint, sizeof(hint), "  pair_code: %s", code);
        centered_text(hint, 194, 0x4da6ff, TINY);
    }

    drawButtonLegends("Back", "", "");
    refreshDisplay();
}

ESPNowPairingScene espnowPairingScene;

#endif
