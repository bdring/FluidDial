// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for the Arduino framework

#include "System.h"
#include "FluidNCModel.h"
#include "NVS.h"
#include "BootLog.h"

#include <Esp.h>  // ESP.restart()

#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    include "PeerLink.h"
#endif

// ── UART transport ─────────────────────────────────────────────────────────────
// Always compiled so the pendant can operate over UART even in a WiFi build
// when the user has selected UART mode at runtime.
#include <driver/uart.h>
#include "hal/uart_hal.h"

uart_port_t fnc_uart_port;

#ifdef LED_DEBUG
static void ledcolor(int n) {
    digitalWrite(4, !(n & 1));
    digitalWrite(16, !(n & 2));
    digitalWrite(17, !(n & 4));
}
#endif

static void uart_putchar_impl(uint8_t c) {
    uart_write_bytes(fnc_uart_port, (const char*)&c, 1);
#ifdef ECHO_FNC_TO_DEBUG
    dbg_write(c);
#endif
}

// Bytes are read from the driver in blocks; one uart_read_bytes() call per
// byte is too slow to keep up with FluidNC at 1 Mbaud.
static uint8_t s_rx_buf[256];
static int     s_rx_len = 0;
static int     s_rx_pos = 0;

static int uart_getchar_impl() {
    if (s_rx_pos >= s_rx_len) {
        s_rx_pos = 0;
        s_rx_len = uart_read_bytes(fnc_uart_port, s_rx_buf, sizeof(s_rx_buf), 0);
        if (s_rx_len <= 0) {
            s_rx_len = 0;
            return -1;
        }
    }
    uint8_t c = s_rx_buf[s_rx_pos++];
#ifdef LED_DEBUG
    if (c == '\r' || c == '\n') { ledcolor(0); }
    else                        { ledcolor(c & 7); }
#endif
    update_rx_time();
#ifdef ECHO_FNC_TO_DEBUG
    dbg_write(c);
#endif
    return c;
}

// True if a received byte is buffered, here or in the UART driver
static bool uart_rx_waiting() {
    if (s_rx_pos < s_rx_len) {
        return true;
    }
    size_t n = 0;
    uart_get_buffered_data_len(fnc_uart_port, &n);
    return n > 0;
}

#ifdef USE_WIFI
extern "C" void fnc_putchar(uint8_t c) {
    if (wifi_use_uart_mode())   { uart_putchar_impl(c); return; }
    if (wifi_use_espnow_mode()) { espnow_putchar(c);    return; }
    ws_putchar(c);
}
extern "C" int fnc_getchar() {
    int ch;
    if (wifi_use_uart_mode()) {
        ch = uart_getchar_impl();
    } else if (wifi_use_espnow_mode()) {
        ch = espnow_getchar();
    } else {
        ch = ws_getchar();
    }
    return observe_fnc_rx(ch);
}
// Whether another received byte is already buffered for the active transport
extern "C" bool fnc_rx_waiting() {
    if (wifi_use_uart_mode())   return uart_rx_waiting();
    if (wifi_use_espnow_mode()) return espnow_rx_available();
    return ws_rx_available();
}
#else
// ── UART-only build ───────────────────────────────────────────────────────────
extern "C" void fnc_putchar(uint8_t c) { uart_putchar_impl(c); }
extern "C" bool fnc_rx_waiting()        { return uart_rx_waiting(); }
extern "C" int  fnc_getchar()          { return observe_fnc_rx(uart_getchar_impl()); }
#endif

// poll_extra: called by fnc_poll() inside fnc_send_line()'s blocking wait loop.
// In WiFi mode this MUST drive wifi_poll() so the Telnet socket refills _rx_buf
// and the "ok" response from FluidNC can be observed — otherwise fnc_send_line()
// blocks for the full 2000 ms timeout on every second jog command.
extern "C" void poll_extra() {
#ifdef USE_WIFI
    if (wifi_use_espnow_mode()) {
        espnow_poll();
    } else if (!wifi_use_uart_mode()) {
        wifi_poll();
    } else {
        // Over UART, wifi_poll() only services OTA. fnc_poll() calls this once
        // per received byte, so rate-limit it or the reader falls behind.
        static uint32_t last_ms = 0;
        uint32_t        now     = millis();
        if (now - last_ms >= 20) {
            last_ms = now;
            wifi_poll();
        }
    }
#endif
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
    drawPngFile(&canvas, filename, x, y);
}
void drawPngFile(LGFX_Sprite* sprite, const char* filename, int x, int y) {
    // When datum is middle_center, the origin is the center of the canvas and the
    // +Y direction is down.
    std::string fn { "/" };
    fn += filename;
    sprite->drawPngFile(LittleFS, fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

#define FORMAT_LITTLEFS_IF_FAILED true

// Baud rates up to 10M work
#ifndef FNC_BAUD
#    define FNC_BAUD 1000000
#endif

extern void init_hardware();

void init_fnc_uart(int uart_num, int tx_pin, int rx_pin) {
    fnc_uart_port = (uart_port_t)uart_num;
    int baudrate  = FNC_BAUD;
    uart_driver_delete(fnc_uart_port);
    s_rx_len = s_rx_pos = 0;
    uart_set_pin(fnc_uart_port, (gpio_num_t)tx_pin, (gpio_num_t)rx_pin, -1, -1);
    uart_config_t conf;
#    if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
    conf.source_clk = UART_SCLK_APB;  // ESP32, ESP32S2
#    endif
#    if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // UART_SCLK_XTAL is independent of the APB frequency
    conf.source_clk = UART_SCLK_XTAL;  // ESP32C3, ESP32S3
#    endif
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
    // Large enough to absorb a whole preferences.json while the loop is busy
    uart_driver_install(fnc_uart_port, 16384, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    uart_set_sw_flow_ctrl(fnc_uart_port, true, 64, 120);
    uint32_t baud;
    uart_get_baudrate(fnc_uart_port, &baud);
    bootlog_printf("uart: num=%d tx=%d rx=%d baud=%lu", uart_num, tx_pin, rx_pin, (unsigned long)baud);
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
#ifdef USE_WIFI
    if (!wifi_use_uart_mode()) return;  // no-op over WiFi/Telnet
#endif
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
