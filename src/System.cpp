// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FluidNCModel.h"

#if 0
// Helpful for debugging touch development.
const char* M5TouchStateName(m5::touch_state_t state_num) {
    static constexpr const char* state_name[16] = { "none", "touch", "touch_end", "touch_begin", "___", "hold", "hold_end", "hold_begin",
                                                    "___",  "flick", "flick_end", "flick_begin", "___", "drag", "drag_end", "drag_begin" };

    return state_name[state_num];
}
#endif

void dbg_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

void dbg_print(const std::string& s) {
    dbg_print(s.c_str());
}

void dbg_println(const std::string& s) {
    dbg_println(s.c_str());
}

void dbg_println(const char* s) {
    dbg_print(s);
    dbg_print("\r\n");
}
