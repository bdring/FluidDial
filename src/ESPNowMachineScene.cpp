// 2026 - Figamore

#ifdef USE_WIFI

#include "ESPNowMachineScene.h"
#include "ESPNowPairingScene.h"
#include "PeerLink.h"
#include "WiFiSetupScene.h"
#include "Drawing.h"
#include "System.h"

#include <stdio.h>
#include <string.h>
#include <string>

static constexpr int ITEM_H_ROUND     = 30;
static constexpr int ITEM_PITCH_ROUND = 31;
static constexpr int ITEM_H_CYD       = 34;
static constexpr int ITEM_PITCH_CYD   = 36;
static constexpr int START_Y_ROUND    = 40;
static constexpr int START_Y_CYD      = 34;

static void mac_text(const uint8_t mac[6], uint8_t channel, char* out, size_t out_len) {
    snprintf(out, out_len,
             "%02x:%02x:%02x:%02x:%02x:%02x  ch %u",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             (unsigned)channel);
}

int ESPNowMachineScene::itemCount() const {
    return _profile_count + 1;  // saved profiles + Pair New
}

void ESPNowMachineScene::onEntry(void* /*arg*/) {
    _profile_count = (int)espnow_profile_count();
    if (_selected >= itemCount()) {
        _selected = itemCount() - 1;
    }
    if (_selected < 0) {
        _selected = 0;
    }
    reDisplay();
}

void ESPNowMachineScene::onEncoder(int delta) {
    int count = itemCount();
    if (count <= 0) {
        return;
    }
    _selected = (_selected + delta + count) % count;
    reDisplay();
}

void ESPNowMachineScene::activateSelected() {
    if (_selected < _profile_count) {
        if (espnow_select_profile((size_t)_selected)) {
            if (parent_scene()) {
                pop_scene();
            } else {
                activate_scene(&wifiSetupScene);
            }
        } else {
            _profile_count = (int)espnow_profile_count();
            if (_selected >= itemCount()) {
                _selected = itemCount() - 1;
            }
            reDisplay();
        }
        return;
    }

    push_scene(&espnowPairingScene);
}

void ESPNowMachineScene::onRedButtonPress() {
    if (parent_scene()) {
        pop_scene();
    } else {
        activate_scene(&wifiSetupScene);
    }
}

void ESPNowMachineScene::onGreenButtonPress() { activateSelected(); }
void ESPNowMachineScene::onDialButtonPress()  { activateSelected(); }

void ESPNowMachineScene::onTouchClick() {
    int item_h     = round_display ? ITEM_H_ROUND : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y    = round_display ? START_Y_ROUND : START_Y_CYD;
    int bx         = round_display ? 28 : 18;
    int bw         = round_display ? 184 : 204;

    for (int i = 0; i < itemCount(); ++i) {
        int y = start_y + i * item_pitch;
        if (touchX >= bx && touchX <= bx + bw &&
            touchY >= y - 3 && touchY < y + item_h + 3) {
            _selected = i;
            reDisplay();
            activateSelected();
            return;
        }
    }
}

void ESPNowMachineScene::reDisplay() {
    _profile_count = (int)espnow_profile_count();
    if (_selected >= itemCount()) {
        _selected = itemCount() - 1;
    }
    if (_selected < 0) {
        _selected = 0;
    }

    int item_h     = round_display ? ITEM_H_ROUND : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y    = round_display ? START_Y_ROUND : START_Y_CYD;
    int bx         = round_display ? 28 : 18;
    int bw         = round_display ? 184 : 204;

    background();

    if (round_display) {
        centered_text("Machines", 18);
        drawRect(70, 28, 100, 1, 0, DARKGREY);
    } else {
        centered_text("ESP-NOW Machines", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    for (int i = 0; i < itemCount(); ++i) {
        int  y   = start_y + i * item_pitch;
        bool sel = i == _selected;

        if (sel) {
            drawOutlinedRect(bx, y - 4, bw, item_h, 0x1a001a, 0xcc66ff);
        }

        if (i < _profile_count) {
            ESPNowProfileInfo profile;
            espnow_get_profile((size_t)i, profile);

            char title[40];
            if (profile.hostname[0]) {
                snprintf(title, sizeof(title), "%s", profile.hostname);
            } else {
                snprintf(title, sizeof(title), "Machine %d", i + 1);
            }

            char detail[40];
            mac_text(profile.mac, profile.channel, detail, sizeof(detail));

            if (profile.active) {
                drawFilledCircle(bx + 8, y + item_h / 2 - 1, 3, 0xcc66ff);
            }

            int title_x = bx + (profile.active ? 22 : 10);
            auto_text(std::string(title), title_x, y + 9, bw - 24, sel ? WHITE : LIGHTGREY, SMALL, middle_left);
            auto_text(std::string(detail), title_x, y + 24, bw - 24, sel ? 0xcc66ff : DARKGREY, TINY, middle_left);
        } else {
            centered_text("Pair New", y + item_h / 2, sel ? WHITE : 0xcc66ff, SMALL);
        }
    }

    drawButtonLegends("Back", _selected < _profile_count ? "Use" : "Pair", "");
    refreshDisplay();
}

ESPNowMachineScene espnowMachineScene;

#endif  // USE_WIFI
