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

const int n_buttons       = 3;
int       button_colors[] = { RED, YELLOW, GREEN };
class Layout {
public:
    const char* _name;
    int         _rotation;
    Point       _spritePosition;
    Point       _buttonPosition[3];
    Layout(const char* name, int rotation, Point spritePosition, Point redPosition, Point yellowPosition, Point greenPosition) :
        _name(name), _rotation(rotation), _spritePosition(spritePosition) {
        _buttonPosition[0] = redPosition;
        _buttonPosition[1] = yellowPosition;
        _buttonPosition[2] = greenPosition;
    }
};
Layout layouts[] = {
    { "up", 0, { 0, 0 }, { 0, 240 }, { 80, 240 }, { 160, 240 } },     // Buttons above
    { "up", 0, { 0, 80 }, { 0, 0 }, { 80, 0 }, { 160, 0 } },          // Buttons below
    { "left", 1, { 0, 0 }, { 240, 0 }, { 240, 80 }, { 240, 160 } },   // Buttons right
    { "left", 1, { 80, 0 }, { 0, 0 }, { 0, 80 }, { 0, 160 } },        // Buttons left
    { "down", 2, { 0, 0 }, { 0, 240 }, { 80, 240 }, { 160, 240 } },   // Buttons below
    { "down", 2, { 0, 80 }, { 0, 0 }, { 80, 0 }, { 160, 0 } },        // Buttons above
    { "right", 3, { 80, 0 }, { 0, 0 }, { 0, 80 }, { 0, 160 } },       // Buttons left
    { "right", 3, { 0, 0 }, { 240, 0 }, { 240, 80 }, { 240, 160 } },  // Buttons right
};
Layout* layout;
int     layout_num = 0;

Point sprite_offset;
void  set_layout(int n) {
    layout = &layouts[n];
    display.setRotation(layout->_rotation);
    sprite_offset = layout->_spritePosition;
}

nvs_handle_t hw_nvs;
void         init_hardware() {
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
    Point offset = layout->_buttonPosition[n];
    display.fillRoundRect(offset.x + 10, offset.y + 10, 60, 60, 10, button_colors[n]);
}

void base_display() {
    display.clear();
    display.drawPngFile(LittleFS, "/fluid_dial.png", sprite_offset.x, sprite_offset.y, 240, 240, 0, 0, 0.0f, 0.0f, datum_t::middle_center);

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
bool hit(int button_num, int x, int y) {
    Point offset = layout->_buttonPosition[button_num];
    int   px     = offset.x;
    int   py     = offset.y;
    return x >= px && x < (px + 80) && y >= py && y < (py + 80);
}
bool screen_button_touched(int x, int y, int& button) {
    for (int i = 0; i < n_buttons; i++) {
        if (hit(i, x, y)) {
            button = i;
            return true;
        }
    }
    return false;
}

void update_events() {
    auto ms = lgfx::millis();
    if (touch.isEnabled()) {
        touch.update(ms);
    }
}

void ackBeep() {}

void deep_sleep(int us) {}
