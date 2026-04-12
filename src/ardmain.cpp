// Copyright (c) 2023 -	Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "FileParser.h"
#include "Scene.h"
#include "AboutScene.h"

#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    include "WiFiSetupScene.h"
extern Scene firstBootScene;
static bool _wifi_initialized  = false;
static bool _first_boot_active = false;
#endif

extern void base_display();
extern void show_logo();

extern const char* git_info;

extern AboutScene aboutScene;

void setup() {
    init_system();

    display.setBrightness(aboutScene.getBrightness());

    show_logo();
    delay_ms(1000);  // view the logo and wait for the debug port to connect

    base_display();

    dbg_printf("FluidNC Pendant %s\n", git_info);

#ifndef USE_WIFI
    fnc_realtime(StatusReport);  // Kick FluidNC into action via UART
#else
    if (wifi_use_uart_mode()) {
        fnc_realtime(StatusReport);  // UART mode in a WiFi build
    }
#endif

    extern Scene* initMenus();
    Scene* menu = initMenus();

#ifdef USE_WIFI
    // On first boot (no transport mode saved yet) show the setup wizard
    // immediately — before the main menu — so the user is never silently
    // dropped into WiFi mode.  The wizard restarts the device on selection,
    // so wifi_init() is deferred until the next normal boot.
    if (wifi_is_first_boot()) {
        _first_boot_active = true;
        activate_scene(&firstBootScene);
    } else {
        activate_scene(menu);
    }
#else
    activate_scene(menu);
#endif
}

void loop() {
#ifdef USE_WIFI
    if (!_first_boot_active) {
        if (!_wifi_initialized) {
        // Defer WiFi init until after setup() has returned + 1st render is complete - ensure sprite caches allocate before WiFi consumes heap.
            _wifi_initialized = true;
            wifi_init();
        }
        wifi_poll();
    }
#endif
    fnc_poll();         // Parse incoming bytes from FluidNC (UART or WebSocket)
    dispatch_events();  // Handle dial, touch, buttons
}
