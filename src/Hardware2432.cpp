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

m5::Touch_Class  xtouch;
m5::Touch_Class& touch = xtouch;

LGFX         xdisplay;
LGFX_Device& display = xdisplay;
LGFX_Sprite  canvas(&xdisplay);

int dial_button_pin = -1;

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
#ifdef DEBUG_TO_USB
    Serial.begin(115200);
#endif
    hw_nvs = nvs_init("hardware");
    nvs_get_i32(hw_nvs, "layout", &layout_num);

    display.init();
    set_layout(layout_num);

    touch.begin(&display);

    int enc_a = -1, enc_b = -1;
    dial_button_pin = -1;

    lgfx::boards::board_t board_id = display.getBoard();
    switch (board_id) {
        case lgfx::boards::board_Guition_ESP32_2432W328:
            enc_a           = GPIO_NUM_16;  // RGB LED Green
            enc_b           = GPIO_NUM_17;  // RGB LED Blue
            dial_button_pin = GPIO_NUM_4;   // RGB LED Red
            pinMode(dial_button_pin, INPUT_PULLUP);
            // backlight = GPIO_NUM_27;
            break;
        case lgfx::boards::board_Sunton_ESP32_2432S028:
            enc_a = GPIO_NUM_22;
            enc_b = GPIO_NUM_27;
            // backlight = GPIO_NUM_21;
            break;
        default:
            dbg_printf("Unknown board id %d\n", board_id);
            break;
    }
    init_encoder(enc_a, enc_b);
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
    switch (n) {
        case 0:
            display.drawPngFile(LittleFS, "/red_button.png", offset.x + 10, offset.y + 10, 60, 60, 0, 0, 0.0f, 0.0f, datum_t::top_left);
            break;
        case 1:
            display.drawPngFile(LittleFS, "/orange_button.png", offset.x + 10, offset.y + 10, 60, 60, 0, 0, 0.0f, 0.0f, datum_t::top_left);
            break;
        case 2:
            display.drawPngFile(LittleFS, "/green_button.png", offset.x + 10, offset.y + 10, 60, 60, 0, 0, 0.0f, 0.0f, datum_t::top_left);
            break;
    }
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
    static int last_state = -1;
    bool       state      = digitalRead(dial_button_pin);
    if ((int)state != last_state) {
        last_state = state;
        button     = 1;
        pressed    = !state;
        return true;
    }
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
