// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"

extern void base_display();

extern const char* git_info;

void setup() {
    init_system();

    base_display();

    delay_ms(1000);  // view the logo and wait for the debug port to connect

    dbg_printf("FluidNC Pendant %s\n", git_info);

    fnc_realtime(StatusReport);  // Kick FluidNC into action

    // init_file_list();

    extern Scene* initMenus();
    activate_scene(initMenus());
}

void loop() {
    fnc_poll();         // Handle messages from FluidNC
    dispatch_events();  // Handle dial, touch, buttons
}
