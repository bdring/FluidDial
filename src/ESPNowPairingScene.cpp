// 2026 - Figamore

#ifdef USE_WIFI

#include "ESPNowPairingScene.h"
#include "PeerLink.h"
#include "WiFiConnection.h"
#include "Drawing.h"
#include "System.h"
#include "Scene.h"
#include <stdio.h>

extern Scene menuScene;


static void draw_code(const char* code) {
    char left[4]  = {code[0], code[1], code[2], '\0'};
    char right[4] = {code[3], code[4], code[5], '\0'};

    if (round_display) {
        text(left,  85, 115, WHITE, LARGE, middle_right);
        text("-",  120, 115, DARKGREY, LARGE, middle_center);
        text(right, 155, 115, WHITE, LARGE, middle_left);
    } else {
        text(left,  130, 115, WHITE, LARGE, middle_right);
        text("-",   160, 115, DARKGREY, LARGE, middle_center);
        text(right, 190, 115, WHITE, LARGE, middle_left);
    }
}

void ESPNowPairingScene::onEntry(void* /*arg*/) {
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
    pop_scene();
}

void ESPNowPairingScene::onPoll() {
    if (espnow_pairing_complete()) {
        background();
        drawMenuTitle("ESP-NOW");
        if (round_display) {
            centered_text("Paired!", 100, GREEN, MEDIUM);
            centered_text("Connected to FluidNC", 130, LIGHTGREY, TINY);
        } else {
            centered_text("Paired!", 100, GREEN, MEDIUM);
            centered_text("Connected to FluidNC", 130, LIGHTGREY, SMALL);
        }
        refreshDisplay();
        delay_ms(1500);
        activate_scene(&menuScene);
        return;
    }
    reDisplay();
}

void ESPNowPairingScene::reDisplay() {
    background();

    if (round_display) {
        centered_text("ESP-NOW Setup", 18);
        drawRect(60, 28, 120, 1, 0, DARKGREY);
    } else {
        centered_text("ESP-NOW Setup", 12);
        drawRect(55, 22, 130, 1, 0, DARKGREY);
    }

    const char* code = espnow_pairing_code();

    if (round_display) {
        centered_text("Enter in FluidNC", 52, LIGHTGREY, TINY);
        centered_text("config.yaml:", 66, LIGHTGREY, TINY);
        draw_code(code);
        centered_text("espnow:", 148, DARKGREY, TINY);

        char hint[24];
        snprintf(hint, sizeof(hint), " pair_code: %s", code);
        centered_text(hint, 160, 0x4da6ff, TINY);
    } else {
        centered_text("Enter this code in FluidNC:", 44, LIGHTGREY, TINY);
        draw_code(code);
        centered_text("config.yaml  >  espnow:", 148, DARKGREY, TINY);

        char hint[24];
        snprintf(hint, sizeof(hint), "pair_code: %s", code);
        centered_text(hint, 162, 0x4da6ff, SMALL);
    }

    drawButtonLegends("Cancel", "", "");
    refreshDisplay();
}

ESPNowPairingScene espnowPairingScene;

#endif
