// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "M5GFX.h"
#include "Drawing.h"
#include "HardwareM5Dial.hpp"

LGFX_Device&       display = M5Dial.Display;
LGFX_Sprite        canvas(&M5Dial.Display);
m5::Speaker_Class& speaker   = M5Dial.Speaker;
m5::Touch_Class&   touch     = M5Dial.Touch;
Stream&            debugPort = USBSerial;

m5::Button_Class& dialButton = M5Dial.BtnA;
m5::Button_Class  greenButton;
m5::Button_Class  redButton;

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

    init_fnc_uart(FNC_UART_NUM, FNC_TX_PIN, FNC_RX_PIN);

    // Setup external GPIOs as buttons
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, RED_BUTTON_PIN);
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, GREEN_BUTTON_PIN);

    greenButton.setDebounceThresh(5);
    redButton.setDebounceThresh(5);

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
    if (redButton.wasPressed()) {
        button  = 0;
        pressed = true;
        return true;
    }
    if (redButton.wasReleased()) {
        button  = 0;
        pressed = false;
        return true;
    }
    if (dialButton.wasPressed()) {
        button  = 1;
        pressed = true;
        return true;
    }
    if (dialButton.wasReleased()) {
        button  = 1;
        pressed = false;
        return true;
    }
    if (greenButton.wasPressed()) {
        button  = 2;
        pressed = true;
        return true;
    }
    if (greenButton.wasReleased()) {
        button  = 2;
        pressed = false;
        return true;
    }
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

    // The red and green buttons are active low
    redButton.setRawState(ms, !m5gfx::gpio_in(RED_BUTTON_PIN));
    greenButton.setRawState(ms, !m5gfx::gpio_in(GREEN_BUTTON_PIN));
}

void ackBeep() {
    speaker.tone(1800, 50);
}

#include <driver/rtc_io.h>
// The M5 Library is broken with respect to deep sleep on M5 Dial
// so we have to do it ourselves.  The problem is that the WAKE
// button is supposed to be the dial button that connects to GPIO42,
// but that can't work because GPIO42 is not an RTC GPIO and thus
// cannot be used as an ext0 wakeup source.
void deep_sleep(int us) {
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
}
