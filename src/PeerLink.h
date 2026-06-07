// 2026 - Figamore

#pragma once
#ifdef USE_WIFI

#include <stdint.h>
#include <stddef.h>

static constexpr size_t ESPNOW_PROFILE_HOSTNAME_SIZE = 32;

struct ESPNowProfileInfo {
    uint8_t mac[6];
    uint8_t channel;
    bool    active;
    char    hostname[ESPNOW_PROFILE_HOSTNAME_SIZE];
};

void espnow_init();
void espnow_poll();


void espnow_putchar(uint8_t c);
int  espnow_getchar();  // returns -1 if no data
bool espnow_rx_available();  // true if a received byte is buffered


bool espnow_is_paired();
bool espnow_is_connected();
const char* espnow_status_str();
void espnow_start_pairing();
void espnow_cancel_pairing();
bool espnow_pairing_complete();
void espnow_clear_pairing();
bool espnow_has_saved_pairing();
bool espnow_is_reconnecting();
int8_t espnow_rssi();
int espnow_signal_bars();
size_t espnow_profile_count();
int espnow_active_profile_index();
bool espnow_get_profile(size_t index, ESPNowProfileInfo& out);
bool espnow_select_profile(size_t index);
bool espnow_remove_profile(size_t index);

#endif
