#pragma once

// WiFi/WebSocket connection layer for FluidDial
// Replaces UART (fnc_putchar/fnc_getchar) with WebSocket transport

#ifdef ARDUINO

struct WiFiConfig {
    char ssid[64];
    char password[64];
    char fluidnc_ip[40];
    bool valid;
};

// Initialise WiFi. If no credentials saved, starts AP setup mode.
void wifi_init();

// Must be called from main loop() — processes WebSocket events,
// DNS and HTTP requests in AP mode, ping timers, etc.
void wifi_poll();

bool wifi_is_connected();       // ESP32 STA joined the network
bool websocket_is_connected();  // WebSocket connection to FluidNC is up
bool wifi_in_ap_mode();         // Running as access point for initial setup

// Start a captive-portal AP named "FluidDial".
// User connects to it, browses to 192.168.4.1 and fills in the form.
void wifi_start_ap_setup();

// Stop the AP / HTTP server and restart (called after saving config).
void wifi_stop_ap_and_restart();

// Persistent credential storage (NVS via Preferences).
void       wifi_save_config(const char* ssid, const char* password, const char* ip);
WiFiConfig wifi_load_config();

// Convenience accessors for display.
const char* wifi_ap_ssid();
const char* wifi_status_str();

// Returns signal strength as 0–4 bars (0 = no WiFi / disconnected).
int wifi_signal_bars();

#endif  // ARDUINO
