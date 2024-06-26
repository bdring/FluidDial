// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include "Scene.h"
#include "e4math.h"

class ProbingScene : public Scene {
private:
    int  selection   = 0;
    long oldPosition = 0;

    // Saved to NVS
    e4_t _offset  = e4_from_int(0);
    int  _travel  = -20;
    int  _rate    = 80;
    int  _retract = 20;
    int  _axis    = 2;  // Z is default

public:
    ProbingScene() : Scene("Probe") {}

    void onDialButtonPress() { pop_scene(); }

    void onGreenButtonPress() {
        // G38.2 G91 F80 Z-20 P8.00
        switch (state) {
            case Idle:
                send_linef("G38.2G91F%d%c%dP%s", _rate, axisNumToChar(_axis), _travel, e4_to_cstr(_offset, 2));
                break;
            case Cycle:
                fnc_realtime(FeedHold);
                break;
            case Hold:
            case DoorClosed:
                fnc_realtime(CycleStart);
                break;
            case Alarm:
                send_line("$X"); // unlock
                break;
        }
    }

    void onRedButtonPress() {
        // G38.2 G91 F80 Z-20 P8.00
        if (state == Cycle || state == Alarm) {
            fnc_realtime(Reset);            
            return;
        } else if (state == Idle) {
            int retract = _travel < 0 ? _retract : -_retract;
            send_linef("$J=G91F1000%c%d", axisNumToChar(_axis), retract);
            return;
        } else if (state == Hold || state == DoorClosed) {
            fnc_realtime(Reset);
        }
    }

    void onTouchClick() {
        // Rotate through the items to be adjusted.
        rotateNumberLoop(selection, 1, 0, 4);
        reDisplay();
        ackBeep();
    }

    void onDROChange() { reDisplay(); }

    void onEncoder(int delta) {
        if (abs(delta) > 0) {
            switch (selection) {
                case 0:
                    _offset += delta * 100;  // Increment by 0.0100
                    setPref("Offset", _offset);
                    break;
                case 1:
                    _travel += delta;
                    setPref("Travel", _travel);
                    break;
                case 2:
                    _rate += delta;
                    if (_rate < 1) {
                        _rate = 1;
                    }
                    setPref("Rate", _rate);
                    break;
                case 3:
                    _retract += delta;
                    if (_retract < 0) {
                        _retract = 0;
                    }
                    setPref("Retract", _retract);
                    break;
                case 4:
                    rotateNumberLoop(_axis, 1, 0, 2);
                    setPref("Axis", _axis);
            }
            reDisplay();
        }
    }
    void onEntry(void* arg) override {
        if (initPrefs()) {
            getPref("Offset", &_offset);
            getPref("Travel", &_travel);
            getPref("Rate", &_rate);
            getPref("Retract", &_retract);
            getPref("Axis", &_axis);
        }
    }

    void reDisplay() {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        const char* grnLabel = "";
        const char* redLabel = "";

        if (state == Idle) {
            int    x      = 40;
            int    y      = 62;
            int    width  = display_short_side() - (x * 2);
            int    height = 25;
            int    pitch  = 27;  // for spacing of buttons
            Stripe button(x, y, width, height, TINY);
            button.draw("Offset", e4_to_cstr(_offset, 2), selection == 0);
            button.draw("Max Travel", intToCStr(_travel), selection == 1);
            y = button.y();  // For LED
            button.draw("Feed Rate", intToCStr(_rate), selection == 2);

            button.draw("Retract", intToCStr(_retract), selection == 3);
            button.draw("Axis", axisNumToCStr(_axis), selection == 4);

            //LED led(x - 20, y + height / 2, 10, button.gap());
            //led.draw(myProbeSwitch);

            grnLabel = "Probe";
            redLabel = "Retract";
        } else {
            if (state == Jog || state == Alarm) {  // there is no Probing state, so Cycle is a valid state on this
                //centered_text("Invalid State", 105, WHITE, MEDIUM);
                //centered_text("For Probing", 145, WHITE, MEDIUM);
                redLabel = "Reset";
                grnLabel = "Unlock";
            } else {
                int x      = 14;
                int height = 35;
                int y      = 82 - height / 2;

                LED led(120, 190, 10, 5);
                led.draw(myProbeSwitch);

                int width = display_short_side() - x * 2;
                DRO dro(x, y, width, height);
                dro.draw(0, _axis == 0);
                dro.draw(1, _axis == 1);
                dro.draw(2, _axis == 2);

                switch (state) {
                    case Cycle:
                        redLabel = "E-Stop";
                        grnLabel = "Hold";
                        break;
                    case Hold:
                    case DoorClosed:
                        redLabel = "Reset";
                        grnLabel = "Resume";
                        break;
                }
            }
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
        drawError();  // only if one just happened
        refreshDisplay();
    }
};
ProbingScene probingScene;
