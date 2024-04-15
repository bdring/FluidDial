// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#ifdef ARDUINO
#    include "System.h"
#    include "FluidNCModel.h"

#    include <Esp.h>  // ESP.restart()

M5GFX&             display = M5Dial.Display;
M5Canvas           canvas(&M5Dial.Display);
m5::Speaker_Class& speaker    = M5Dial.Speaker;
m5::Touch_Class&   touch      = M5Dial.Touch;
m5::Button_Class&  dialButton = M5Dial.BtnA;
Stream&            debugPort  = USBSerial;

#    include <driver/uart.h>
#    include "hal/uart_hal.h"

uart_port_t fnc_uart_num = uart_port_t(1);

m5::Button_Class greenButton;
m5::Button_Class redButton;

// We use the ESP-IDF UART driver instead of the Arduino
// HardwareSerial driver so we can use software (XON/XOFF)
// flow control.  The ESP-IDF driver supports the ESP32's
// hardware implementation of XON/XOFF, but Arduino does not.

extern "C" void fnc_putchar(uint8_t c) {
    uart_write_bytes(fnc_uart_num, (const char*)&c, 1);
}

extern "C" int fnc_getchar() {
    char c;
    int  res = uart_read_bytes(fnc_uart_num, &c, 1, 0);
    if (res == 1) {
        update_rx_time();
#    ifdef ECHO_FNC_TO_DEBUG
        dbg_write(c);
#    endif
        return c;
    }
    return -1;
}

extern "C" void poll_extra() {
    if (debugPort.available()) {
        char c = debugPort.read();
        if (c == 0x12) {  // CTRL-R
#    ifdef ARDUINO
            ESP.restart();
            while (1) {}
#    endif
        }
        fnc_putchar(c);  // So you can type commands to FluidNC
    }
}

void drawPngFile(const char* filename, int x, int y) {
    // When datum is middle_center, the origin is the center of the canvas and the
    // +Y direction is down.
    std::string fn { "/" };
    fn += filename;
    canvas.drawPngFile(LittleFS, fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

#    define FORMAT_LITTLEFS_IF_FAILED true

// Baud rates up to 10M work
#    ifndef FNC_BAUD
#        define FNC_BAUD 115200
#    endif

void init_system() {
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

    uart_set_pin(fnc_uart_num, FNC_TX_PIN, FNC_RX_PIN, -1, -1);
    int baudrate = FNC_BAUD;
    uart_driver_delete(fnc_uart_num);
    uart_config_t conf;
    // UART_SCLK_XTAL is independent of the APB frequency
    conf.source_clk = UART_SCLK_XTAL;  // ESP32C3, ESP32S3
    // conf.source_clk = UART_SCLK_APB;  // ESP32, ESP32S2
    conf.baud_rate = baudrate;

    conf.data_bits           = UART_DATA_8_BITS;
    conf.parity              = UART_PARITY_DISABLE;
    conf.stop_bits           = UART_STOP_BITS_1;
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    if (uart_param_config(fnc_uart_num, &conf) != ESP_OK) {
        dbg_println("UART config failed");
        while (1) {}
        return;
    };
    uart_driver_install(fnc_uart_num, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    uart_set_sw_flow_ctrl(fnc_uart_num, true, 64, 120);
    uint32_t baud;
    uart_get_baudrate(fnc_uart_num, &baud);
    printf("Baud %d\n", baud);

    // Setup external GPIOs as buttons
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, RED_BUTTON_PIN);
    lgfx::gpio::command(lgfx::gpio::command_mode_input_pullup, GREEN_BUTTON_PIN);

    greenButton.setDebounceThresh(5);
    redButton.setDebounceThresh(5);

    touch.setFlickThresh(30);

    init_encoder();  // Use our own encoder driver

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        dbg_println("LittleFS Mount Failed");
        return;
    }

    // Make an offscreen canvas that can be copied to the screen all at once
    canvas.createSprite(display.width(), display.height());

    // Draw the logo screen
    display.clear();
    display.drawPngFile(LittleFS, "/fluid_dial.png", 0, 0, display.width(), display.height(), 0, 0, 0.0f, 0.0f, datum_t::middle_center);

    speaker.setVolume(255);
}
void resetFlowControl() {
    fnc_putchar(0x11);
    uart_ll_force_xon(fnc_uart_num);
}

void update_events() {
    M5Dial.update();

    auto ms = m5gfx::millis();

    // The red and green buttons are active low
    redButton.setRawState(ms, !m5gfx::gpio_in(RED_BUTTON_PIN));
    greenButton.setRawState(ms, !m5gfx::gpio_in(GREEN_BUTTON_PIN));
}

void delay_ms(uint32_t ms) {
    delay(ms);
}

void dbg_write(uint8_t c) {
#    ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > 1) {
        debugPort.write(c);
    }
#    endif
}

void dbg_print(const char* s) {
#    ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > strlen(s)) {
        debugPort.print(s);
    }
#    endif
}
#endif
