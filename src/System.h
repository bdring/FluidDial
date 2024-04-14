// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#ifdef ARDUINO
#    include <Arduino.h>
#    include <LittleFS.h>

#    include "M5Dial.h"
#    include "Encoder.h"

#    ifdef UART_ON_PORT_B
constexpr static const int RED_BUTTON_PIN   = GPIO_NUM_13;
constexpr static const int GREEN_BUTTON_PIN = GPIO_NUM_15;
constexpr static const int FNC_RX_PIN       = GPIO_NUM_1;
constexpr static const int FNC_TX_PIN       = GPIO_NUM_2;

#    else  // UART is on PORT A
// This pin assignment avoids a problem whereby touch will not work
// if the pendant is powered independently of the FluidNC controller
// and the pendant is power-cycled while the FluidNC controller is on.
// The problem is caused by back-powering of the 3V3 rail through the
// M5Stamp's Rx pin.  When RX is on GPIO1, 3V3 from the FluidNC Tx line
// causes the M5Stamp 3V3 rail to float at 1.35V, which in turn prevents
// the touch chip from starting properly after full power is applied.
// The touch function then does not work.
// When RX is on GPIO15, the back-powering drives the 3V3 rail only to
// 0.3V and everything starts correctly.
constexpr static const int RED_BUTTON_PIN   = GPIO_NUM_1;
constexpr static const int GREEN_BUTTON_PIN = GPIO_NUM_2;
constexpr static const int FNC_RX_PIN       = GPIO_NUM_15;
constexpr static const int FNC_TX_PIN       = GPIO_NUM_13;

#    endif

constexpr static const int DIAL_BUTTON_PIN = GPIO_NUM_42;
constexpr static const int UPDATE_RATE_MS  = 30;  // minimum refresh rate in milliseconds

extern Stream& debugPort;

extern m5::Button_Class greenButton;
extern m5::Button_Class redButton;
#else
#    include "M5Unified.h"
#    include "Encoder.h"
extern m5::Button_Class& greenButton;
extern m5::Button_Class& redButton;
#endif

#ifdef WINDOWSxx
#    include "LGFX_SDL.hpp"
extern LGFX        display;
extern LGFX_Sprite canvas;
#else
extern M5GFX&            display;
extern M5Canvas          canvas;
#endif
extern m5::Speaker_Class& speaker;
extern m5::Touch_Class&   touch;

extern m5::Button_Class& dialButton;

void drawPngFile(const char* filename, int x, int y);

const char* M5TouchStateName(m5::touch_state_t state_num);

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
