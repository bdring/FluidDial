// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"

extern void base_display();
void setup() {
    init_system();

    delay_ms(1000);  // view the logo and wait for the debug port to connect

    base_display();
    fnc_realtime(StatusReport);  // Kick FluidNC into action

    dbg_println("FluidNC Pendant v0.4");

    // init_file_list();

    extern Scene* initMenus();
    activate_scene(initMenus());
}

void loop() {
    fnc_poll();         // Handle messages from FluidNC
    dispatch_events();  // Handle dial, touch, buttons
}
