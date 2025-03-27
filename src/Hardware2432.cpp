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

LGFX_Sprite buttons(&xdisplay);
#ifdef LOCKOUT_PIN
LGFX_Sprite locked_buttons(&xdisplay);
#endif

int red_button_pin   = -1;
int dial_button_pin  = -1;
int green_button_pin = -1;

#ifdef DEBUG_TO_USB
Stream& debugPort = Serial;
#endif

bool round_display = false;

const int n_buttons      = 3;
const int button_w       = 80;
const int button_h       = 80;
const int button_half_wh = button_w / 2;
const int sprite_wh      = 240;
Point     button_wh(button_w, button_h);

int button_colors[] = { RED, YELLOW, GREEN };
class Layout {
private:
    int _rotation;

public:
    Point buttonsXY;
    Point buttonsWidth;
    Point buttonsHeight;
    Point buttonsWH;
    Point spritePosition;
#if 0
    Point buttonPosition[3];
#endif
    Layout(int rotation, Point spritePosition, Point firstButtonPosition) :
        _rotation(rotation), spritePosition(spritePosition), buttonsXY(firstButtonPosition) {
        if (_rotation & 1) {  // Vertical
            buttonsWH = { button_w, sprite_wh };
        } else {
            buttonsWH = { sprite_wh, button_h };
        }
#if 0
        buttonPosition[0] = buttonsXY;
        if (_rotation & 1) { // Vertical
            int x             = buttonPosition[0].x;
            buttonPosition[1] = { x, button_h };
            buttonPosition[2] = { x, 2 * button_h };
        } else {
            int y             = buttonPosition[0].y;
            buttonPosition[1] = { button_w, y };
            buttonPosition[2] = { 2 * button_w, y };
        }
#endif
    }
    Point buttonOffset(int n) {
        return (_rotation & 1) ? Point(0, n * button_h) : Point(n * button_w, 0);
    }

    int rotation() {
        return _rotation;
    }
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
    red_button_pin   = -1;
    dial_button_pin  = -1;
    green_button_pin = -1;

    lgfx::boards::board_t board_id = display.getBoard();
    switch (board_id) {
        case lgfx::boards::board_Guition_ESP32_2432W328:
#ifdef LOCKOUT_PIN
            pinMode(LOCKOUT_PIN, INPUT);
#endif

#ifdef CYD_BUTTONS
            enc_a = GPIO_NUM_22;
            enc_b = GPIO_NUM_21;
            // rotary_button_pin = GPIO_NUM_35;
            // pinMode(rotary_button_pin, INPUT);  // Pullup does not work on GPIO35

            red_button_pin   = GPIO_NUM_4;   // RGB LED Red
            dial_button_pin  = GPIO_NUM_17;  // RGB LED Blue
            green_button_pin = GPIO_NUM_16;  // RGB LED Green
            pinMode(red_button_pin, INPUT_PULLUP);
            pinMode(dial_button_pin, INPUT_PULLUP);
            pinMode(green_button_pin, INPUT_PULLUP);
#else
            enc_a = GPIO_NUM_22;
            enc_b = GPIO_NUM_17;  // RGB LED Blue
#endif
            // backlight = GPIO_NUM_27;
            break;
        case lgfx::boards::board_Sunton_ESP32_2432S028:
            enc_a = GPIO_NUM_22;
            enc_b = GPIO_NUM_27;
            break;
        default:
            dbg_printf("Unknown board id %d\n", board_id);
            break;
    }
    init_encoder(enc_a, enc_b);
    init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);

    touch.setFlickThresh(10);

#ifdef LED_DEBUG
    // RGB LED pins
    pinMode(4, OUTPUT);   // Red
    pinMode(16, OUTPUT);  // Green
    pinMode(17, OUTPUT);  // Blue
#endif
}

void initButton(int n) {
    Point offset = layout->buttonOffset(n);
    buttons.fillRect(offset.x, offset.y, 80, 80, BLACK);
    const int   radius = 28;
    const char* filename;
    int         color;
    switch (n) {
        case 0:
            color    = RED;
            filename = "/red_button.png";
            break;
        case 1:
            color    = YELLOW;
            filename = "/orange_button.png";
            break;
        case 2:
            color    = GREEN;
            filename = "/green_button.png";
            break;
    }
    buttons.fillCircle(offset.x + button_half_wh, offset.y + button_half_wh, radius, color);
    // If the image file exists the image will overwrite the circle
    buttons.drawPngFile(LittleFS, filename, offset.x + 10, offset.y + 10, 60, 60, 0, 0, 0.0f, 0.0f, datum_t::top_left);
}

void redrawButtons(LGFX_Sprite& sprite) {
    display.startWrite();
    Point position = layout->buttonsXY;
    sprite.pushSprite(position.x, position.y);
    display.endWrite();
}

#ifdef LOCKOUT_PIN
void initLockedButtons() {
    locked_buttons.setColorDepth(display.getColorDepth());
    locked_buttons.createSprite(layout->buttonsWH.x, layout->buttonsWH.y);

    locked_buttons.fillRect(0, 0, layout->buttonsWH.x, layout->buttonsWH.y, BLACK);

    const int radius = 28;
    for (int i = 0; i < 3; i++) {
        Point offset = layout->buttonOffset(i);
        locked_buttons.fillCircle(offset.x + button_half_wh, offset.y + button_half_wh, radius, DARKGREY);
    }
}
#endif

static void initButtons() {
    buttons.setColorDepth(display.getColorDepth());
    buttons.createSprite(layout->buttonsWH.x, layout->buttonsWH.y);

    // On-screen buttons
    for (int i = 0; i < 3; i++) {
        initButton(i);
    }
}

void base_display() {
    display.clear();
    display.drawPngFile(
        LittleFS, "/fluid_dial.png", sprite_offset.x, sprite_offset.y, sprite_wh, sprite_wh, 0, 0, 0.0f, 0.0f, datum_t::middle_center);

    initButtons();
#ifdef LOCKOUT_PIN
    initLockedButtons();
#endif
    redrawButtons(buttons);
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
    static int last_red   = -1;
    static int last_green = -1;
    static int last_dial  = -1;
    bool       state;
    if (red_button_pin != -1) {
        state = digitalRead(red_button_pin);
        if ((int)state != last_red) {
            last_red = state;
            button   = 0;
            pressed  = !state;
            return true;
        }
    }
    if (dial_button_pin != -1) {
        state = digitalRead(dial_button_pin);
        if ((int)state != last_dial) {
            last_dial = state;
            button    = 1;
            pressed   = !state;
            return true;
        }
    }
    if (green_button_pin != -1) {
        state = digitalRead(green_button_pin);
        if ((int)state != last_green) {
            last_green = state;
            button     = 2;
            pressed    = !state;
            return true;
        }
    }
    return false;
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}

struct button_debounce_t {
    bool    debouncing;
    bool    skipped;
    int32_t timeout;
} debounce[n_buttons] = { { false, false, 0 } };

bool    touch_debounce = false;
int32_t touch_timeout  = 0;

bool ui_locked() {
#ifdef LOCKOUT_PIN
    static int last_lock = -1;
    bool       state     = digitalRead(LOCKOUT_PIN);
    if ((int)state != last_lock) {
        last_lock = state;
        redrawButtons(state ? locked_buttons : buttons);
    }
    return state;
#else
    return false;
#endif
}

bool in_rect(Point test, Point xy, Point wh) {
    return test.x >= xy.x && test.x < (xy.x + wh.x) && test.y >= xy.y && test.y < (xy.y + wh.y);
}
bool in_button_stripe(Point xy) {
    return in_rect(xy, layout->buttonsXY, layout->buttonsWH);
}
bool screen_button_touched(bool pressed, int x, int y, int& button) {
    Point xy(x, y);
    if (!in_button_stripe(xy)) {
        return false;
    }
    xy -= layout->buttonsXY;
    for (int i = 0; i < n_buttons; i++) {
        if (in_rect(xy, layout->buttonOffset(i), button_wh)) {
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
