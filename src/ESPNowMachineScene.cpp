// 2026 - Figamore

#ifdef USE_WIFI

#include "ESPNowMachineScene.h"
#include "ESPNowPairingScene.h"
#include "ConfirmScene.h"
#include "PeerLink.h"
#include "WiFiSetupScene.h"
#include "Drawing.h"
#include "System.h"

#include <stdio.h>
#include <string.h>
#include <string>

static constexpr int ITEM_H_ROUND     = 40;
static constexpr int ITEM_PITCH_ROUND = 50;
static constexpr int ITEM_H_CYD       = 34;
static constexpr int ITEM_PITCH_CYD   = 44;
static constexpr int START_Y_ROUND    = 40;
static constexpr int START_Y_CYD      = 34;

static void mac_text(const uint8_t mac[6], char* out, size_t out_len) {
    snprintf(out, out_len,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int visible_item_count() {
    return round_display ? 3 : 4;
}

static int first_visible_item(int selected, int total) {
    int visible = visible_item_count();
    if (total <= visible) {
        return 0;
    }

    int first = selected - visible / 2;
    if (first < 0) {
        first = 0;
    }

    int last_first = total - visible;
    if (first > last_first) {
        first = last_first;
    }
    return first;
}

int ESPNowMachineScene::itemCount() const {
    return _profile_count + 1;  // saved profiles + Pair New
}

void ESPNowMachineScene::onEntry(void* arg) {
    if (arg && strcmp((const char*)arg, "Confirmed") == 0) {
        deletePendingProfile();
    } else {
        _pending_delete = -1;
    }
    _profile_count = (int)espnow_profile_count();
    if (_selected >= itemCount()) {
        _selected = itemCount() - 1;
    }
    if (_selected < 0) {
        _selected = 0;
    }
    reDisplay();
}

void ESPNowMachineScene::confirmDeleteSelected() {
    if (_selected < 0 || _selected >= _profile_count) {
        return;
    }

    ESPNowProfileInfo profile;
    if (!espnow_get_profile((size_t)_selected, profile)) {
        reDisplay();
        return;
    }

    _pending_delete = _selected;
    const char* name = profile.hostname[0] ? profile.hostname : "this machine";
    snprintf(_confirm_message, sizeof(_confirm_message),
             "Unpair this \nmachine?", name);
    push_scene(&confirmScene, _confirm_message);
}

void ESPNowMachineScene::deletePendingProfile() {
    int index = _pending_delete;
    _pending_delete = -1;
    if (index < 0 || !espnow_remove_profile((size_t)index)) {
        return;
    }

    _profile_count = (int)espnow_profile_count();
    if (_selected >= _profile_count) {
        _selected = _profile_count > 0 ? _profile_count - 1 : 0;
    }
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
void ESPNowMachineScene::onDialButtonPress()  { confirmDeleteSelected(); }

void ESPNowMachineScene::onTouchClick() {
    int item_h     = round_display ? ITEM_H_ROUND : ITEM_H_CYD;
    int item_pitch = round_display ? ITEM_PITCH_ROUND : ITEM_PITCH_CYD;
    int start_y    = round_display ? START_Y_ROUND : START_Y_CYD;
    int bx         = round_display ? 28 : 18;
    int bw         = round_display ? 184 : 204;

    int total = itemCount();
    int first = first_visible_item(_selected, total);
    int last  = first + visible_item_count();
    if (last > total) {
        last = total;
    }

    for (int i = first; i < last; ++i) {
        int y = start_y + (i - first) * item_pitch;
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

    int total = itemCount();
    int first = first_visible_item(_selected, total);
    int last  = first + visible_item_count();
    if (last > total) {
        last = total;
    }

    for (int i = first; i < last; ++i) {
        int  y   = start_y + (i - first) * item_pitch;
        bool sel = i == _selected;

        if (sel) {
            drawOutlinedRect(bx + (round_display ? 7 : 0), y - 10, bw - (round_display ? 14 : 0), item_h + 14, 0x001a4d, 0x4da6ff);
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
            mac_text(profile.mac, detail, sizeof(detail));

            if (profile.active) {
                drawFilledCircle(bx + (round_display ? 14 : 6), y + (round_display ? 17 : 10), 3, 0xcc66ff);
            }

            int title_x  = bx + (profile.active ? 22 : 10);
            int title_y  = y + (round_display ? 9 : 8);
            int detail_y = y + (round_display ? 29 : 23);
            int title_w  = bw - (title_x - bx) - 10;

            auto_text(std::string(title), title_x, title_y, title_w,
                      sel ? WHITE : LIGHTGREY, SMALL, middle_left);
            centered_text(detail, detail_y + 4,
                          sel ? 0xcc66ff : DARKGREY, TINY);
        } else {
            centered_text("Pair New", y + item_h / 2, sel ? WHITE : 0xcc66ff, SMALL);
        }
    }

    drawButtonLegends("Back",
                      _selected < _profile_count ? "Use" : "Pair",
                      _selected < _profile_count ? "Forget" : "");
    refreshDisplay();
}

ESPNowMachineScene espnowMachineScene;

#endif  // USE_WIFI
