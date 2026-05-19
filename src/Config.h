#pragma once

// Instead of editing this file, consider adding -D lines in platformio.ini

// System Interface

// #define ECHO_FNC_TO_DEBUG

// #define UART_ON_PORT_B // Not recommended, see comment in System.h

// Automatically go to Jog Scene when first connected
// #define AUTO_JOG_SCENE

// Automatically go to Homing Scene when unhomed alarm is present
// #define AUTO_HOMING_SCENE

// Automatically leave Homing Scene after homing is finished
// #define AUTO_HOMING_RETURN

// Trace incoming bytes from FluidNC (ok / error:N / [rx-other] / JSON
// chunk sizes + brace depth + macro-chain advance points) via dbg_printf.
// Useful when diagnosing wire-protocol issues over Telnet vs UART.
// Uncomment to enable; zero runtime cost when commented out.
// #define FNC_RX_TRACE
