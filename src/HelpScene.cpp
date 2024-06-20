// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"

static const char* null_help[] = { "", "HelpScene:", "Null pointer", NULL };
class HelpScene : public Scene {
public:
    HelpScene() : Scene("Help") {}
    void onEntry(void* arg) {
        const char** msg = arg ? static_cast<const char**>(arg) : null_help;
        const char*  line;
        drawBackground(BROWN);
        int pos = 20;
        for (; line = *msg, line; ++msg) {
            centered_text(line, pos, WHITE, TINY);
            pos += 28;
        }
        drawButtonLegends("", "", "Back");
        refreshDisplay();
    }
    void onDialButtonPress() { pop_scene(); }
    void onTouchClick() {
        if (touchIsCenter()) {
            pop_scene();
        }
    }
} helpScene;
