// 2026 - Figamore

#pragma once
#ifdef USE_WIFI

#include <stdint.h>
#include <stddef.h>

void espnow_init();
void espnow_poll();


void espnow_putchar(uint8_t c);
int  espnow_getchar();  // returns -1 if no data
bool espnow_rx_available();  // true if a received byte is buffered


bool espnow_is_paired();
bool espnow_is_connected();
const char* espnow_status_str();
const char* espnow_start_pairing();
void espnow_cancel_pairing();
bool espnow_pairing_complete();
const char* espnow_pairing_code();
uint32_t espnow_code_remaining_ms();
void espnow_clear_pairing();
bool espnow_has_saved_pairing();
bool espnow_is_reconnecting();
int8_t espnow_rssi();
int espnow_signal_bars();

#endif
