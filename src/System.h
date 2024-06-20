// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#include "Encoder.h"

#ifdef ARDUINO
#    include <Arduino.h>
#    include <LittleFS.h>
constexpr static const int UPDATE_RATE_MS = 30;  // minimum refresh rate in milliseconds
extern Stream&             debugPort;
void                       init_fnc_uart(int uart_num, int tx_pin, int rx_pin);
#endif  // ARDUINO

#ifdef USE_LOVYANGFX
#    include "LovyanGFX.h"
#    include "Touch_Class.hpp"

#    define WHITE TFT_WHITE
#    define BLACK TFT_BLACK
#    define RED TFT_RED
#    define YELLOW TFT_YELLOW
#    define BLUE TFT_BLUE
#    define LIGHTGREY TFT_LIGHTGREY
#    define DARKGREY TFT_DARKGREY
#    define GREEN TFT_GREEN
#    define NAVY TFT_NAVY
#    define CYAN TFT_CYAN
#    define ORANGE TFT_ORANGE
#    define BROWN TFT_BROWN
#    define MAROON TFT_MAROON
#endif  // USE_LOVYANGFX

#ifdef USE_M5
#    include "M5Unified.h"
#endif  // USE_M5

extern LGFX_Device&     display;
extern LGFX_Sprite      canvas;
extern m5::Touch_Class& touch;

void drawPngFile(const char* filename, int x, int y);

void init_system();

void ackBeep();

void dbg_write(uint8_t c);
void dbg_print(const char* s);
void dbg_println(const char* s);
void dbg_print(const std::string& s);
void dbg_println(const std::string& s);
void dbg_printf(const char* format, ...);

void update_events();
void delay_ms(uint32_t ms);

void resetFlowControl();

extern bool round_display;

void system_background();

bool screen_encoder(int x, int y, int& delta);
bool screen_button_touched(bool pressed, int x, int y, int& button);
bool switch_button_touched(bool& pressed, int& button);

void deep_sleep(int us);

inline int display_short_side() {
    return (display.width() < display.height()) ? display.width() : display.height();
}

void base_display();
void set_layout(int n);
void next_layout(int delta);
