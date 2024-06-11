// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "ConfigItem.h"

extern Scene statusScene;

#define HOMING_N_AXIS 3

IntConfigItem homing_cycles[HOMING_N_AXIS] = {
    { "$/axes/x/homing/cycle" },
    { "$/axes/y/homing/cycle" },
    { "$/axes/z/homing/cycle" },
};
BoolConfigItem homing_allows[HOMING_N_AXIS] = {
    { "$/axes/x/homing/allow_single_axis" },
    { "$/axes/y/homing/allow_single_axis" },
    { "$/axes/z/homing/allow_single_axis" },
};

int  homed_axes = 0;
bool is_homed(int axis) {
    return homed_axes & (1 << axis);
}
void set_axis_homed(int axis) {
    homed_axes |= 1 << axis;
    current_scene->reDisplay();
}

void detect_homing_info() {
    for (int i = 0; i < HOMING_N_AXIS; i++) {
        homing_cycles[i].init();
        homing_allows[i].init();
    }
    homed_axes = 0;
}
bool can_home(int i) {
    // Cannot home if cycle == 0 and !allow_single_axis
    return homing_cycles[i].get() != 0 || homing_allows[i].get();
}

bool have_homing_info() {
    return homing_allows[HOMING_N_AXIS - 1].known();
}

class HomingScene : public Scene {
private:
    int _axis_to_home = -1;
    int _auto         = false;

    bool _allows[HOMING_N_AXIS];

public:
    HomingScene() : Scene("Home", 4) {}

    bool is_homing(int axis) { return can_home(axis) && (_axis_to_home == -1 || _axis_to_home == axis); }
    void onEntry(void* arg) override {
        if (state == Idle && _auto) {
            pop_scene();
        }
        const char* s = static_cast<const char*>(arg);
        _auto         = s && strcmp(s, "auto") == 0;
    }

    void onStateChange(state_t old_state) override {
#ifdef AUTO_HOMING_RETURN
        if (old_state == Homing && state == Idle && _auto) {
            pop_scene();
        }
#endif
    }
    void onDialButtonPress() override { pop_scene(); }
    void onGreenButtonPress() override {
        if (state == Idle || state == Alarm) {
            if (_axis_to_home != -1) {
                send_linef("$H%c", axisNumToChar(_axis_to_home));
            } else {
                send_line("$H");
            }
        } else if (state == Cycle) {
            fnc_realtime(FeedHold);
        } else if (state == Hold || state == DoorClosed) {
            fnc_realtime(CycleStart);
        }
    }
    void onRedButtonPress() override {
        if (state == Homing || state == Alarm) {
            fnc_realtime(Reset);
        }
    }

    void increment_axis_to_home() {
        do {
            ++_axis_to_home;
            if (_axis_to_home > HOMING_N_AXIS) {
                _axis_to_home = -1;
                return;
            }
        } while (!can_home(_axis_to_home));
    }
    void onTouchClick() {
        if (state == Idle || state == Homing || state == Alarm) {
            increment_axis_to_home();
            reDisplay();
            ackBeep();
        }
    }

    void onEncoder(int delta) override {
        increment_axis_to_home();
        reDisplay();
    }
    void onDROChange() { reDisplay(); }  // also covers any status change

    void reDisplay() {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        const char* redLabel    = "";
        std::string grnLabel    = "";
        const char* orangeLabel = "";
        std::string green       = "Home ";

        if (false && state == Homing) {
            DRO dro(16, 68, 210, 32);
            for (size_t axis = 0; axis < HOMING_N_AXIS; axis++) {
                dro.draw(axis, -1, true);
            }

        } else if (state == Idle || state == Homing || state == Alarm) {
            DRO dro(16, 68, 210, 32);
            for (int axis = 0; axis < HOMING_N_AXIS; ++axis) {
                dro.drawHoming(axis, is_homing(axis), is_homed(axis));
            }

#if 0
            int x      = 50;
            int y      = 65;
            int width  = display.width() - (x * 2);
            int height = 32;

            Stripe button(x, y, width, height, SMALL);
            button.draw("Home All", _axis_to_home == -1);
            y = button.y();  // LEDs start with the Home X button
            button.draw("Home X", _axis_to_home == 0);
            button.draw("Home Y", _axis_to_home == 1);
            button.draw("Home Z", _axis_to_home == 2);
            LED led(x - 16, y + height / 2, 10, button.gap());
            led.draw(myLimitSwitches[0]);
            led.draw(myLimitSwitches[1]);
            led.draw(myLimitSwitches[2]);
#endif

            if (state == Homing) {
                redLabel = "E-Stop";
            } else {
                if (state == Alarm && (strchr(myCtrlPins, 'D') == NULL)) {  // You can reset alarms if door is not active
                    redLabel = "Reset";
                }
                if (_axis_to_home == -1) {
                    for (int axis = 0; axis < HOMING_N_AXIS; ++axis) {
                        if (can_home(axis)) {
                            if (!grnLabel.length()) {
                                grnLabel = "Home";
                            }

                            grnLabel += axisNumToChar(axis);
                        }
                    }
                } else {
                    grnLabel = "Home";
                    grnLabel += axisNumToChar(_axis_to_home);
                }
            }
        } else {
            centered_text("Invalid State", 105, WHITE, MEDIUM);
            centered_text("For Homing", 145, WHITE, MEDIUM);
            redLabel = "E-Stop";
            if (state == Cycle) {
                grnLabel = "Hold";
            } else if (state == Hold || state == DoorClosed) {
                grnLabel = "Resume";
            }
        }
        drawButtonLegends(redLabel, grnLabel.c_str(), "Back");

        refreshDisplay();
    }
};
HomingScene homingScene;
