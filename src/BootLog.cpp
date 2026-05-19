// Copyright (c) 2026 - Paul Mokbel
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "BootLog.h"
#include "GrblParserC.h"  // milliseconds()

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char s_lines[BOOTLOG_NUM_LINES][BOOTLOG_LINE_LEN];
static int  s_head  = 0;  // index of next slot to write
static int  s_count = 0;  // number of valid entries (capped)

extern "C" void bootlog_printf(const char* fmt, ...) {
    char* slot = s_lines[s_head];
    int   ms   = milliseconds();
    int   n    = snprintf(slot, BOOTLOG_LINE_LEN, "[%6d] ", ms);
    if (n < 0 || n >= BOOTLOG_LINE_LEN) {
        slot[BOOTLOG_LINE_LEN - 1] = '\0';
    } else {
        va_list args;
        va_start(args, fmt);
        vsnprintf(slot + n, BOOTLOG_LINE_LEN - n, fmt, args);
        va_end(args);
    }
    slot[BOOTLOG_LINE_LEN - 1] = '\0';
    s_head = (s_head + 1) % BOOTLOG_NUM_LINES;
    if (s_count < BOOTLOG_NUM_LINES) {
        s_count++;
    }
}

extern "C" const char* bootlog_line(int i) {
    if (i < 0 || i >= s_count) {
        return NULL;
    }
    // i==0 is the newest; head points to the next-write slot, so newest is at head-1
    int idx = (s_head - 1 - i + BOOTLOG_NUM_LINES * 2) % BOOTLOG_NUM_LINES;
    return s_lines[idx];
}

extern "C" int bootlog_count(void) {
    return s_count;
}
