// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "ConfirmScene.h"

extern Scene menuScene;

class StatusScene : public Scene {
private:
    const char* _entry = nullptr;

    // the fro/sro/rt rotating display state
    typedef enum {
        FRO,
        SRO,
        RT_FEED_SPEED,
    } ovrd_display_t;

    ovrd_display_t overd_display = FRO;

public:
    StatusScene() : Scene("Status") {}

    void onExit() override {}

    void onEntry(void* arg) override {
        // Returned from ConfirmScene — execute the deferred soft reset.
        if (arg && strcmp((const char*)arg, "Confirmed") == 0) {
            dbg_printf("StatusScene: sending Ctrl-X soft reset\r\n");
            fnc_realtime(Reset);
            schedule_action([]() { send_line("$X"); });
        } else {
            dbg_printf("StatusScene: onEntry arg=%s\r\n", arg ? (const char*)arg : "null");
        }
    }

    void onDialButtonPress() {
        if (state == Cycle || state == Hold) {
            if (overd_display == FRO)
                fnc_realtime(FeedOvrReset);
            else if (overd_display == SRO)
                fnc_realtime(SpindleOvrReset);
        } else {
            pop_scene();
        }
    }

    void onStateChange(state_t old_state) {
        if (old_state == Cycle && state == Idle && parent_scene() != &menuScene) {
            pop_scene();
        }
    }

    void onTouchClick() {
        if (touchY > 150 && (state == Cycle || state == Hold)) {
            switch (overd_display) {
                case FRO:
                    overd_display = SRO;
                    break;
                case SRO:
                    overd_display = RT_FEED_SPEED;
                    break;
                case RT_FEED_SPEED:
                    overd_display = FRO;
            }
            reDisplay();
        }
        fnc_realtime(StatusReport);  // sometimes you want an extra status
    }

    void onRedButtonPress() {
        switch (state) {
            case Alarm:
                if (alarm_is_critical()) {
                    // Soft reset loses work offsets — ask the user to confirm first.
                    push_scene(&confirmScene, (void*)"Soft Reset?\nOffsets will be lost");
                } else {
                    // Non-critical alarm that can be soft-cleared
                    send_line("$X");
                }
                break;
            case Cycle:
            case Homing:
            case Hold:
            case DoorClosed:
                fnc_realtime(Reset);
                break;
        }
    }

    bool alarm_is_homing() { return lastAlarm == 14 || (lastAlarm >= 6 && lastAlarm <= 9); }
    bool alarm_is_critical() {
        switch (lastAlarm) {
            case 4: case 5:                  // Probe fail
            case 6: case 7: case 8: case 9: // Homing fail
            case 14:                         // Unhomed
                return false;
            default:
                return true;
        }
    }
    void onGreenButtonPress() {
        switch (state) {
            case Cycle:
                fnc_realtime(FeedHold);
                break;
            case Hold:
            case DoorClosed:
                fnc_realtime(CycleStart);
                break;
            case Alarm:
                if (alarm_is_homing()) {
                    send_line("$H");
                }
                break;
        }
        fnc_realtime(StatusReport);
    }

    void onEncoder(int delta) {
        if (state == Cycle) {
            switch (overd_display) {
                case FRO:
                    if (delta > 0 && myFro < 200) {
                        fnc_realtime(FeedOvrFinePlus);
                    } else if (delta < 0 && myFro > 10) {
                        fnc_realtime(FeedOvrFineMinus);
                    }
                    break;
                case SRO:
                    if (delta > 0 && mySro < 200) {
                        fnc_realtime(SpindleOvrFinePlus);
                    } else if (delta < 0 && mySro > 10) {
                        fnc_realtime(SpindleOvrFineMinus);
                    }
                    break;
                case RT_FEED_SPEED:
                    overd_display = FRO;
            }

            reDisplay();
        }
    }

    void onDROChange() { reDisplay(); }
    void onLimitsChange() { reDisplay(); }

    void reDisplay() {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        DRO dro(16, 68, 210, 32);
        dro.draw(0, -1, true);
        dro.draw(1, -1, true);
        dro.draw(2, -1, true);

        int y = 170;
        if (state == Cycle || state == Hold) {
            int width  = 192;
            int height = 10;
            if (myPercent > 0) {
                drawRect(20, y, width, height, 5, LIGHTGREY);
                width = (width * myPercent) / 100;
                if (width > 0) {
                    drawRect(20, y, width, height, 5, GREEN);
                }
            }
            // Feed override
            char legend[50];
            switch (overd_display) {
                case FRO:
                    sprintf(legend, "Feed Rate Ovr:%d%%", myFro);
                    break;
                case SRO:
                    sprintf(legend, "Spindle Ovr:%d%%", mySro);
                    break;
                case RT_FEED_SPEED:
                    sprintf(legend, "Fd:%d Spd:%d", myFeed, mySpeed);
            }
            centered_text(legend, y + 23);
        } else {
            centered_text(mode_string(), y + 23, GREEN, TINY);
        }

        const char* encoder_button_text = "Menu";

        const char* grnLabel    = "";
        const char* redLabel    = "";
        const char* yellowLabel = "Back";

        switch (state) {
            case Alarm:
                if (alarm_is_critical()) {
                    redLabel = "Reset";
                } else {
                    redLabel = "Unlock";
                }
                if (alarm_is_homing()) {
                    grnLabel = "Home All";
                }
                break;
            case Homing:
                redLabel = "Reset";
                break;
            case Cycle:
                redLabel    = "E-Stop";
                grnLabel    = "Hold";
                yellowLabel = "Rst Ovr";
                break;
            case Hold:
            case DoorClosed:
                redLabel    = "Quit";
                grnLabel    = "Resume";
                yellowLabel = "Rst Ovr";
                break;
            case Jog:
                redLabel = "Jog Cancel";
                break;
            case Idle:
                break;
        }
        drawButtonLegends(redLabel, grnLabel, yellowLabel);

#ifdef USE_WIFI
        if (round_display) {
            drawWiFiSignalBars(70, 20);
        }
#endif
        refreshDisplay();
    }
};
StatusScene statusScene;
