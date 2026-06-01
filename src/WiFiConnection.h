#pragma once

// WiFi/WebSocket connection layer for FluidDial
// Replaces UART (fnc_putchar/fnc_getchar) with WebSocket transport

#ifdef USE_WIFI

#include <stdint.h>

enum class TransportMode : uint8_t {
    UART    = 0,
    WIFI    = 1,
    ESPNOW  = 2,
};

struct WiFiConfig {
    char ssid[64];
    char password[64];
    char fluidnc_ip[40];
    bool valid;
};

// Initialise WiFi. If no credentials saved and auto_ap is true, starts AP setup mode.
void wifi_init(bool auto_ap = true);

// Must be called from main loop() — processes WebSocket events,
// DNS and HTTP requests in AP mode, ping timers, etc.
void wifi_poll();

bool wifi_is_connected();       // ESP32 STA joined the network
bool websocket_is_connected();  // WebSocket connection to FluidNC is up

// Force-close the WebSocket so it can reconnect cleanly.
// Called when fnc_is_connected() declares FluidNC unresponsive.
void wifi_force_ws_reconnect();

void wifi_shutdown();
bool wifi_in_ap_mode();         // Running as access point for initial setup

// Start a captive-portal AP named "FluidDial".
// User connects to it, browses to 192.168.4.1 and fills in the form.
void wifi_start_ap_setup();

// Stop the AP / HTTP server and restart (called after saving config).
void wifi_stop_ap_and_restart();

// Stop the AP / HTTP server without restarting (used when the user wants
// to navigate back to the settings view to switch transport mode).
void wifi_stop_ap();

// Persistent credential storage (NVS via Preferences).
void       wifi_save_config(const char* ssid, const char* password, const char* ip);
WiFiConfig wifi_load_config();

// Convenience accessors for display.
const char* wifi_ap_ssid();
const char* wifi_status_str();
const bool wifi_not_ready();
// Returns signal strength as 0–4 bars (0 = no WiFi / disconnected).
int wifi_signal_bars();

const char* wifi_last_error();

WiFiConfig wifi_active_config();

// ── WebSocket transport primitives (used by fnc_putchar/fnc_getchar routing) ──
// Send one byte to FluidNC via WebSocket.
void ws_putchar(uint8_t c);
// Receive one byte from the WebSocket ring buffer (-1 if empty).
int  ws_getchar();
// True if a received byte is already buffered (no socket read needed).
bool ws_rx_available();

TransportMode wifi_get_transport();
void          wifi_set_transport(TransportMode mode);

bool wifi_use_uart_mode();
bool wifi_use_espnow_mode();

void wifi_set_uart_mode(bool uart);

bool wifi_is_first_boot();

// --- OTA UPDATE ---
void        wifi_start_ota_server();
void        wifi_stop_ota_server();
bool        wifi_ota_server_active();
bool        wifi_ota_ap_mode();
bool        wifi_ota_sta_connected();
int         wifi_ota_progress();      // -1=error, 0=waiting, 1-99=uploading, 100=done
const char* wifi_ota_ip();
const char* wifi_ota_error();
// Re-enter AP credential entry after a failed STA connect (pendant retry).
void        wifi_ota_force_ap_setup();

void        wifi_request_ota_reboot();
bool        wifi_ota_boot_requested();

#endif  // USE_WIFI
