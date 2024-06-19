// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include <LGFX_AUTODETECT.hpp>
#include "Hardware2432.hpp"
#include "Drawing.h"
#include "NVS.h"

#include <driver/uart.h>
#include "hal/uart_hal.h"

// m5::Speaker_Class& speaker    = M5.Speaker;
m5::Touch_Class  xtouch;
m5::Touch_Class& touch = xtouch;
// m5::Button_Class&  dialButton = M5.BtnB;
LGFX         xdisplay;
LGFX_Device& display = xdisplay;
LGFX_Sprite  canvas(&xdisplay);

#ifdef DEBUG_TO_USB
Stream& debugPort = Serial;
#endif

bool round_display = false;

const int n_buttons = 3;
const int button_w  = 80;
const int button_h  = 80;
const int sprite_wh = 240;

int button_colors[] = { RED, YELLOW, GREEN };
class Layout {
private:
    int _rotation;

public:
    Point spritePosition;
    Point buttonPosition[3];
    Layout(int rotation, Point spritePosition, Point firstButtonPosition) : _rotation(rotation), spritePosition(spritePosition) {
        buttonPosition[0] = firstButtonPosition;
        if (vertical_buttons()) {
            int x             = buttonPosition[0].x;
            buttonPosition[1] = { x, button_h };
            buttonPosition[2] = { x, 2 * button_h };
        } else {
            int y             = buttonPosition[0].y;
            buttonPosition[1] = { button_w, y };
            buttonPosition[2] = { 2 * button_w, y };
        }
    }
    bool vertical_buttons() { return _rotation & 1; }
    int  rotation() { return _rotation; }
};

// clang-format off
Layout layouts[] = {
// rotation  sprite_XY        button0_XY
    { 0,     { 0, 0 },        { 0, sprite_wh } }, // Buttons above
    { 0,     { 0, button_h }, { 0, 0 }         }, // Buttons below
    { 1,     { 0, 0 },        { sprite_wh, 0 } }, // Buttons right
    { 1,     { button_w, 0 }, { 0, 0 }         }, // Buttons left
    { 2,     { 0, 0 },        { 0, sprite_wh } }, // Buttons below
    { 2,     { 0, button_h }, { 0, 0 }         }, // Buttons above
    { 3,     { button_w, 0 }, { 0, 0 }         }, // Buttons left
    { 3,     { 0, 0 },        { sprite_wh, 0 } }, // Buttons right
};
// clang-format on

Layout* layout;
int     layout_num = 0;

Point sprite_offset;
void  set_layout(int n) {
     layout = &layouts[n];
     display.setRotation(layout->rotation());
     sprite_offset = layout->spritePosition;
}

nvs_handle_t hw_nvs;

void init_hardware() {
    hw_nvs = nvs_init("hardware");
    nvs_get_i32(hw_nvs, "layout", &layout_num);

    display.init();
    set_layout(layout_num);

    touch.begin(&display);
#ifdef DEBUG_TO_USB
    Serial.begin(115200);
#endif
    init_encoder(ENC_A, ENC_B);
    init_fnc_uart(FNC_UART_NUM, FNC_TX_PIN, FNC_RX_PIN);

    touch.setFlickThresh(10);

#ifdef LED_DEBUG
    // RGB LED pins
    pinMode(4, OUTPUT);
    pinMode(16, OUTPUT);
    pinMode(17, OUTPUT);
#endif
}

void drawButton(int n) {
    Point offset = layout->buttonPosition[n];
    display.fillRoundRect(offset.x + 10, offset.y + 10, 60, 60, 10, button_colors[n]);
}

void base_display() {
    display.clear();
    display.drawPngFile(
        LittleFS, "/fluid_dial.png", sprite_offset.x, sprite_offset.y, sprite_wh, sprite_wh, 0, 0, 0.0f, 0.0f, datum_t::middle_center);

    // On-screen buttons
    for (int i = 0; i < 3; i++) {
        drawButton(i);
    }
}
void next_layout(int delta) {
    layout_num += delta;
    while (layout_num >= 8) {
        layout_num -= 8;
    }
    while (layout_num < 0) {
        layout_num += 8;
    }
    set_layout(layout_num);
    nvs_set_i32(hw_nvs, "layout", layout_num);
    base_display();
}

void system_background() {
    drawBackground(BLACK);
}

bool switch_button_touched(bool& pressed, int& button) {
    return false;
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}
bool in_rect(int x, int y, Point xy, Point wh) {
    return x >= xy.x && x < (xy.x + wh.x) && y >= xy.y && y < (xy.y + wh.y);
}
bool in_button_stripe(int x, int y) {
    Point xy = layout->buttonPosition[0];
    if (layout->vertical_buttons()) {
        // Vertical button layout
        return in_rect(x, y, xy, { button_w, sprite_wh });
    }
    // Horizontal button layout
    return in_rect(x, y, xy, { sprite_wh, button_h });
}
bool hit(int button_num, int x, int y) {
    return in_rect(x, y, layout->buttonPosition[button_num], { button_w, button_h });
}
struct button_debounce_t {
    bool    debouncing;
    bool    skipped;
    int32_t timeout;
} debounce[n_buttons] = { { false, false, 0 } };

bool    touch_debounce = false;
int32_t touch_timeout  = 0;

bool screen_button_touched(bool pressed, int x, int y, int& button) {
    for (int i = 0; i < n_buttons; i++) {
        if (hit(i, x, y)) {
            button = i;
            if (!pressed) {
                touch_debounce = true;
                touch_timeout  = milliseconds() + 100;
            }
            return true;
        }
    }
    return false;
}

void update_events() {
    auto ms = lgfx::millis();
    if (touch.isEnabled()) {
        if (touch_debounce) {
            if ((ms - touch_timeout) < 0) {
                return;
            }
            touch_debounce = false;
        }
        touch.update(ms);
    }
}

void ackBeep() {}

void deep_sleep(int us) {}
