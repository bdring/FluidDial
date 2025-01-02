// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "FluidNCModel.h"
#include "NVS.h"

#include <Esp.h>  // ESP.restart()

#include <driver/uart.h>
#include "hal/uart_hal.h"

uart_port_t fnc_uart_port;

// We use the ESP-IDF UART driver instead of the Arduino
// HardwareSerial driver so we can use software (XON/XOFF)
// flow control.  The ESP-IDF driver supports the ESP32's
// hardware implementation of XON/XOFF, but Arduino does not.

extern "C" void fnc_putchar(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
#ifdef ECHO_FNC_TO_DEBUG
    dbg_write(c);
#endif
}

void ledcolor(int n) {
    digitalWrite(4, !(n & 1));
    digitalWrite(16, !(n & 2));
    digitalWrite(17, !(n & 4));
}
extern "C" int fnc_getchar() {
    char c;
    int  res = uart_read_bytes(fnc_uart_port, &c, 1, 0);
    if (res == 1) {
#ifdef LED_DEBUG
        if (c == '\r' || c == '\n') {
            ledcolor(0);
        } else {
            ledcolor(c & 7);
        }
#endif
        update_rx_time();
#ifdef ECHO_FNC_TO_DEBUG
        dbg_write(c);
#endif
        return c;
    }
    return -1;
}

extern "C" void poll_extra() {
#ifdef DEBUG_TO_USB
    if (debugPort.available()) {
        char c = debugPort.read();
        if (c == 0x12) {  // CTRL-R
            ESP.restart();
            while (1) {}
        }
        fnc_putchar(c);  // So you can type commands to FluidNC
    }
#endif
}

void drawPngFile(const char* filename, int x, int y) {
    // When datum is middle_center, the origin is the center of the canvas and the
    // +Y direction is down.
    std::string fn { "/" };
    fn += filename;
    canvas.drawPngFile(LittleFS, fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

#define FORMAT_LITTLEFS_IF_FAILED true

// Baud rates up to 10M work
#ifndef FNC_BAUD
#    define FNC_BAUD 115200
#endif

extern void init_hardware();

void init_fnc_uart(int uart_num, int tx_pin, int rx_pin) {
    fnc_uart_port = (uart_port_t)uart_num;
    int baudrate  = FNC_BAUD;
    uart_driver_delete(fnc_uart_port);
    uart_set_pin(fnc_uart_port, (gpio_num_t)tx_pin, (gpio_num_t)rx_pin, -1, -1);
    uart_config_t conf;
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
    conf.source_clk = UART_SCLK_APB;  // ESP32, ESP32S2
#endif
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // UART_SCLK_XTAL is independent of the APB frequency
    conf.source_clk = UART_SCLK_XTAL;  // ESP32C3, ESP32S3
#endif
    conf.baud_rate = baudrate;

    conf.data_bits           = UART_DATA_8_BITS;
    conf.parity              = UART_PARITY_DISABLE;
    conf.stop_bits           = UART_STOP_BITS_1;
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    if (uart_param_config(fnc_uart_port, &conf) != ESP_OK) {
        dbg_println("UART config failed");
        while (1) {}
        return;
    };
    uart_driver_install(fnc_uart_port, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    uart_set_sw_flow_ctrl(fnc_uart_port, true, 64, 120);
    uint32_t baud;
    uart_get_baudrate(fnc_uart_port, &baud);
}

void init_system() {
    init_hardware();

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        dbg_println("LittleFS Mount Failed");
        return;
    }

    // Make an offscreen canvas that can be copied to the screen all at once
    canvas.setColorDepth(8);
    canvas.createSprite(240, 240);  // display.width(), display.height());
}
void resetFlowControl() {
    fnc_putchar(0x11);
    uart_ll_force_xon(fnc_uart_port);
}

extern "C" int milliseconds() {
    return millis();
}

void delay_ms(uint32_t ms) {
    delay(ms);
}

void dbg_write(uint8_t c) {
#ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > 1) {
        debugPort.write(c);
    }
#endif
}

void dbg_print(const char* s) {
#ifdef DEBUG_TO_USB
    if (debugPort.availableForWrite() > strlen(s)) {
        debugPort.print(s);
    }
#endif
}

nvs_handle_t nvs_init(const char* name) {
    nvs_handle_t handle;
    esp_err_t    err = nvs_open(name, NVS_READWRITE, &handle);
    return err == ESP_OK ? handle : 0;
}
