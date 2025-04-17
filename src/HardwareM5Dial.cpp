// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "M5GFX.h"
#include "Drawing.h"
#include "HardwareM5Dial.hpp"
#if I2C_BUTTONS
#include <Wire.h>
#include <PCF8574.h>
#endif

LGFX_Device&       display = M5Dial.Display;
LGFX_Sprite        canvas(&M5Dial.Display);
m5::Speaker_Class& speaker   = M5Dial.Speaker;
m5::Touch_Class&   touch     = M5Dial.Touch;
Stream&            debugPort = USBSerial;

m5::Button_Class& dialButton = M5Dial.BtnA;
#ifdef I2C_BUTTONS
// Buttons through the I2C expander
m5::Button_Class buttons_i2c[8];
PCF8574 pcf20(I2C_BUTTONS_ADDR);    // I2C expander 8x DIO
// Map the original button objects to the array elements
m5::Button_Class& greenButton = buttons_i2c[0];
m5::Button_Class& redButton =   buttons_i2c[1];
m5::Button_Class& setXButton =  buttons_i2c[2];
m5::Button_Class& setYButton =  buttons_i2c[3];
m5::Button_Class& setZButton =  buttons_i2c[4];
m5::Button_Class& changeStepButton = buttons_i2c[5];
m5::Button_Class& futureUse1Button = buttons_i2c[6];
m5::Button_Class& futureUse2Button = buttons_i2c[7];
#else
// Two buttons through GPIO pins
m5::Button_Class  greenButton;
m5::Button_Class  redButton;
#endif

bool round_display = true;

void init_hardware() {
    auto cfg = M5.config();

    // Don't enable the encoder because M5's encoder driver is flaky
    M5Dial.begin(cfg, false, false);

    // Turn on the power hold pin
    lgfx::gpio::command(lgfx::gpio::command_mode_output, GPIO_NUM_46);
    lgfx::gpio::command(lgfx::gpio::command_write_high, GPIO_NUM_46);

    // This must be done after M5Dial.begin which sets the PortA pins
    // to I2C mode.  We need to override that to use them for serial.
    // The baud rate is irrelevant because USBSerial emulates a UART
    // API but the data never travels over an actual physical UART
    // link with a defined baud rate.  The data instead travels over
    // a USB link at the USB data rate.  You can set the baud rate
    // at the other end to anything you want and it will still work.
    USBSerial.begin();

    init_fnc_uart(FNC_UART_NUM, PND_TX_FNC_RX_PIN, PND_RX_FNC_TX_PIN);

    #ifdef I2C_BUTTONS
    Wire.begin(I2C_BUTTONS_SDA, I2C_BUTTONS_SCL);
    pcf20.begin();
    for (auto& button : buttons_i2c) {
        button.setDebounceThresh(5);
    }
    #else
    // Setup external GPIOs as buttons
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, RED_BUTTON_PIN);
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, GREEN_BUTTON_PIN);

    greenButton.setDebounceThresh(5);
    redButton.setDebounceThresh(5);
    #endif

    init_encoder(ENC_A, ENC_B);

    speaker.setVolume(255);

    touch.setFlickThresh(30);
}

Point sprite_offset { 0, 0 };

void base_display() {
    display.clear();
    display.drawPngFile(LittleFS, "/fluid_dial.png", 0, 0, display.width(), display.height(), 0, 0, 0.0f, 0.0f, datum_t::middle_center);
}

void next_layout(int delta) {}

void system_background() {
    canvas.fillSprite(TFT_BLACK);
}

bool switch_button_touched(bool& pressed, int& button) {
    if (dialButton.wasPressed()) {
        button  = 0;
        pressed = true;
        return true;
    }
    if (dialButton.wasReleased()) {
        button  = 0;
        pressed = false;
        return true;
    }
    #ifdef I2C_BUTTONS
    for (int i = 0; i < 8; ++i) {
        if (buttons_i2c[i].wasPressed()) {
            button  = i + 1; // Shift index by 1
            pressed = true;
            return true;
        }
        if (buttons_i2c[i].wasReleased()) {
            button  = i + 1; // Shift index by 1
            pressed = false;
            return true;
        }
    }
    #else
    if (greenButton.wasPressed()) {
        button  = 1;
        pressed = true;
        return true;
    }
    if (greenButton.wasReleased()) {
        button  = 1;
        pressed = false;
        return true;
    }
    if (redButton.wasPressed()) {
        button  = 2;
        pressed = true;
        return true;
    }
    if (redButton.wasReleased()) {
        button  = 2;
        pressed = false;
        return true;
    }
    #endif
    // No button pressed or released
    return false;
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}
bool screen_button_touched(bool pressed, int x, int y, int& button) {
    return false;
}

void update_events() {
    M5Dial.update();

    auto ms = m5gfx::millis();

    #ifdef I2C_BUTTONS
    // Read the state of the I2C expander and update button states
    auto but_bits = ~pcf20.read8();    // Active low
    for (int i = 0; i < 8; ++i) {
        bool state = bitRead(but_bits, i);
        buttons_i2c[i].setRawState(ms, state);
    }
    #else
    // The red and green buttons are active low
    redButton.setRawState(ms, !m5gfx::gpio_in(RED_BUTTON_PIN));
    greenButton.setRawState(ms, !m5gfx::gpio_in(GREEN_BUTTON_PIN));
    #endif
}

void ackBeep() {
    speaker.tone(1800, 50);
}

bool ui_locked() {
    return false;
}

#include <driver/rtc_io.h>
// The M5 Library is broken with respect to deep sleep on M5 Dial
// so we have to do it ourselves.  The problem is that the WAKE
// button is supposed to be the dial button that connects to GPIO42,
// but that can't work because GPIO42 is not an RTC GPIO and thus
// cannot be used as an ext0 wakeup source.
void deep_sleep(int us) {
#ifdef WAKEUP_GPIO
    display.sleep();

    rtc_gpio_pullup_en((gpio_num_t)WAKEUP_GPIO);

    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_GPIO, false);
    while (digitalRead(WAKEUP_GPIO) == false) {
        delay_ms(10);
    }
    if (us > 0) {
        esp_sleep_enable_timer_wakeup(us);
    } else {
        // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    }
    esp_deep_sleep_start();
#endif
}
