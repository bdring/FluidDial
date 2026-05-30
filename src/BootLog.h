// Copyright (c) 2026 - Paul Mokbel
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Tiny RAM ring buffer of recent boot/connection diagnostic lines.
// Survives until reset (no flash persistence). Viewable on the About
// scene when the link is down, since UART0 is shared with the
// USB-serial bridge on CYD and cannot be observed concurrently.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define BOOTLOG_LINE_LEN 64
#define BOOTLOG_NUM_LINES 48

void bootlog_printf(const char* fmt, ...);

// Returns the i-th most recent line (0 == newest), or NULL if out of range.
const char* bootlog_line(int i);

// Number of lines currently stored (capped at BOOTLOG_NUM_LINES).
int bootlog_count(void);

#ifdef __cplusplus
}
#endif
