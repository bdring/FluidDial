// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"
#include "AboutScene.h"

#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    include "WiFiSetupScene.h"
#endif

extern void base_display();
extern void show_logo();

extern const char* git_info;

extern AboutScene aboutScene;

void setup() {
    init_system();

    display.setBrightness(aboutScene.getBrightness());

    show_logo();
    delay_ms(2000);  // view the logo and wait for the debug port to connect

    base_display();

    dbg_printf("FluidNC Pendant %s\n", git_info);

#ifndef USE_WIFI
    fnc_realtime(StatusReport);  // Activate FluidNC connection via UART
#endif

    // init_file_list();

    extern Scene* initMenus();
    activate_scene(initMenus());
    // WiFi intentionally NOT started here. Starting WiFi before the first render causes an OOM crash inside new LGFX_Sprite().
    // wifi_init() will be called on the first loop() iteration instead.
}

#ifdef USE_WIFI
static bool _wifi_initialized = false;
#endif

void loop() {
#ifdef USE_WIFI
    if (!_wifi_initialized) {
        // Defer WiFi init until after setup() has returned + 1st render is complete - ensure sprite caches allocate before WiFi consumes heap.
        _wifi_initialized = true;
        wifi_init();
    }
    wifi_poll();
#endif
    fnc_poll();         // Handle messages from FluidNC
    dispatch_events();  // Handle dial, touch, buttons
}
