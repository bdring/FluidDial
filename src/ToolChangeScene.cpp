// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include "Scene.h"
#include "e4math.h"

class ToolChangeScene : public Scene {
private:
    int  selection   = 0;
    long oldPosition = 0;

    // Saved to NVS
    e4_t _offset  = e4_from_int(0);
    int  _travel  = -20;
    int  _rate    = 80;
    int  _retract = 20;
    int  _axis    = 2;  // Z is default

    int _new_tool = 0;

public:
    ToolChangeScene() : Scene("Tools", 4) {}

    void onDialButtonPress() { pop_scene(); }

    void onRedButtonPress() {
        if (state == Idle) {
        } else if (state == Cycle) {
        }

        switch (state) {
            case Idle:
                send_linef("T%d", _new_tool);
                break;

            case Hold:
            case Cycle:
                fnc_realtime(Reset);
                break;

            default:
                break;
        }
    }

    void onGreenButtonPress() {
        switch (state) {
            case Idle:
                send_line("M6");
                break;

            case Hold:
                fnc_realtime(CycleStart);
                break;

            case Cycle:
                fnc_realtime(FeedHold);
                break;

            default:
                break;
        }
    }

    void onStateChange(state_t old_state) { reDisplay(); }

    void onTouchClick() {
        if (state == Idle) {
            send_linef("M61Q%d", _new_tool);
        }
    }

    void onEncoder(int delta) {
        if (abs(delta) > 0) {
            rotateNumberLoop(_new_tool, delta, 0, 255);
            reDisplay();
        }
    }
    void onEntry(void* arg) override {}

    void reDisplay() {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        bool M6Q_button_enabled = false;

        int y         = 80;
        int y_spacing = 30;

        const char* grnLabel = "";
        const char* redLabel = "";
        static char buffer[20];

        sprintf(buffer, "Current T Value: %d", mySelectedTool);
        centered_text(buffer, y, LIGHTGREY, TINY);

        switch (state) {
            case Idle:
                grnLabel = "M6";
                redLabel = "T";

                M6Q_button_enabled = true;

                sprintf(buffer, "T%d", _new_tool);
                redLabel = buffer;
                break;

            case Hold:
                grnLabel = "Resume";
                break;

            case Cycle:
                redLabel = "Reset";
                grnLabel = "Hold";
                break;

            default:
                break;
        }
        int x     = 50;
        int width = display_short_side() - (x * 2);
        if (M6Q_button_enabled) {
            Stripe button(x, 110, width, 50, SMALL);
            button.draw("M61Q", intToCStr(_new_tool), M6Q_button_enabled);
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
        drawError();  // only if one just happened
        refreshDisplay();
    }
};
ToolChangeScene toolchangeScene;
