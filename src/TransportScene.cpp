// 2026 - Figamore

#ifdef USE_WIFI

#include "TransportScene.h"
#include "WiFiConnection.h"
#include "Drawing.h"
#include "System.h"

struct TransportItem {
    const char*   label;
    const char*   sublabel;
    TransportMode mode;
    int           fill;
    int           outline;
};

#ifdef USE_ESPNOW
static constexpr int N_TRANSPORT = 3;
#else
static constexpr int N_TRANSPORT = 2;
#endif

static const TransportItem kItems[N_TRANSPORT] = {
    { "Wired",    "UART serial cable",  TransportMode::UART,   0x001a4d, 0x4da6ff },
    { "WiFi",     "WebSocket / IP",     TransportMode::WIFI,   0x003300, 0x66ff66 },
#ifdef USE_ESPNOW
    { "ESP-NOW",  "No router needed",   TransportMode::ESPNOW, 0x1a001a, 0xcc66ff },
#endif
};

static constexpr int ITEM_H       = 46;
static constexpr int ITEM_PITCH   = 54;
static constexpr int START_Y_ROUND = 46;
static constexpr int START_Y_CYD   = 42;


static int modeIndex(TransportMode m) {
    for (int i = 0; i < N_TRANSPORT; i++) {
        if (kItems[i].mode == m) return i;
    }
    return 0;
}


void TransportScene::onEntry(void* /*arg*/) {
    _selected = modeIndex(wifi_get_transport());
    reDisplay();
}

void TransportScene::onEncoder(int delta) {
    _selected = (_selected + delta + N_TRANSPORT) % N_TRANSPORT;
    reDisplay();
}

void TransportScene::confirmSelection() {
    TransportMode chosen = kItems[_selected].mode;
    if (chosen != wifi_get_transport()) {
        // Gracefully close any live WiFi/Telnet connection (FIN) before the reboot
        wifi_shutdown();
        wifi_set_transport(chosen);
#ifdef ARDUINO
        ESP.restart();
#endif
    } else {
        pop_scene();
    }
}

void TransportScene::onDialButtonPress()  { confirmSelection(); }
void TransportScene::onGreenButtonPress() { confirmSelection(); }
void TransportScene::onRedButtonPress()   { pop_scene(); }

void TransportScene::onTouchClick() {
    int start_y = round_display ? START_Y_ROUND : START_Y_CYD;
    int bx      = round_display ? 28 : 18;
    int bw      = round_display ? 184 : 284;
    for (int i = 0; i < N_TRANSPORT; i++) {
        int y = start_y + i * ITEM_PITCH;
        if (touchX >= bx && touchX <= bx + bw
            && touchY >= y - 2 && touchY < y + ITEM_H + 2) {
            _selected = i;
            reDisplay();
            confirmSelection();
            return;
        }
    }
}

void TransportScene::reDisplay() {
    background();

    if (round_display) {
        centered_text("Transport", 18);
        drawRect(70, 28, 100, 1, 0, DARKGREY);
    } else {
        centered_text("Transport", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    TransportMode cur     = wifi_get_transport();
    int           start_y = round_display ? START_Y_ROUND : START_Y_CYD;
    int           bx      = round_display ? 28 : 18;
    int           bw      = round_display ? 184 : 204;

    for (int i = 0; i < N_TRANSPORT; i++) {
        int  y      = start_y + i * ITEM_PITCH;
        bool sel    = (i == _selected);
        bool active = (kItems[i].mode == cur);

        if (sel) {
            drawOutlinedRect(bx, y - 5, bw, ITEM_H, kItems[i].fill, kItems[i].outline);
        }

        if (active) {
            drawFilledCircle(bx + 8, y + ITEM_H / 2 - 1, 3, kItems[i].outline);
        }

        int label_color = sel ? WHITE : (active ? kItems[i].outline : LIGHTGREY);
        int sub_color   = sel ? kItems[i].outline : DARKGREY;

        centered_text(kItems[i].label,    y + 12, label_color, SMALL);
        centered_text(kItems[i].sublabel, y + 30, sub_color,   TINY);
    }

    drawButtonLegends("Cancel", "Select", "");
    refreshDisplay();
}

TransportScene transportScene;

#endif
