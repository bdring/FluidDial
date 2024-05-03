// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"

extern Scene menuScene;

class StatusScene : public Scene
{
private:
    const char *_entry = nullptr;
    bool _show_fro = true; // or show spindle override

public:
    StatusScene() : Scene("Status") {}

    void onEntry(void *arg) { _entry = static_cast<const char *>(arg); }
    void onExit() override { dbg_println("SoX"); }

    void onDialButtonPress()
    {
        if (state == Cycle || state == Hold)
        {
            _show_fro ? fnc_realtime(FeedOvrReset) : fnc_realtime(SpindleOvrReset);
        }
        else
        {
            pop_scene();
        }
    }

    void onStateChange(state_t old_state)
    {
        if (old_state == Cycle && state == Idle && parent_scene() != &menuScene)
        {
            pop_scene();
        }
    }

    void onTouchClick()
    {
        if (touchY > 150 && touchY < 210 && (state == Cycle || state == Hold))
        {
            _show_fro = !_show_fro;
            reDisplay();
        }
        fnc_realtime(StatusReport); // sometimes you want an extra status
    }

    void onRedButtonPress()
    {
        switch (state)
        {
        case Alarm:
            switch (lastAlarm)
            {
            case 1:  // Hard Limit
            case 2:  // Soft Limit
            case 10: // Spindle Control
            case 13: // Hard Stop
                // Critical alarm that must be hard-cleared with a CTRL-X reset
                // since streaming execution of GCode is blocked
                fnc_realtime(Reset);
                break;
            default:
                // Non-critical alarm that can be soft-cleared
                send_line("$X");
                break;
            }
            break;
        case Cycle:
        case Homing:
        case Hold:
            fnc_realtime(Reset);
            break;
        }
    }

    void onGreenButtonPress()
    {
        switch (state)
        {
        case Cycle:
            fnc_realtime(FeedHold);
            break;
        case Hold:
            fnc_realtime(CycleStart);
            break;
        case Alarm:
            send_line("$H");
            break;
        }
        fnc_realtime(StatusReport);
    }

    void onEncoder(int delta)
    {
        if (state == Cycle)
        {
            if (_show_fro)
            {
                if (delta > 0 && myFro < 200)
                {
                    fnc_realtime(FeedOvrFinePlus);
                }
                else if (delta < 0 && myFro > 10)
                {
                    fnc_realtime(FeedOvrFineMinus);
                }
            }
            else
            {
                if (delta > 0 && mySro < 200)
                {
                    fnc_realtime(SpindleOvrFinePlus);
                }
                else if (delta < 0 && mySro > 10)
                {
                    fnc_realtime(SpindleOvrFineMinus);
                }
            }
            reDisplay();
        }
    }

    void onDROChange() { reDisplay(); }
    void onLimitsChange() { reDisplay(); }

    void reDisplay()
    {
        background();
        drawMenuTitle(current_scene->name());
        drawStatus();

        const char *grnLabel = "";
        const char *redLabel = "";

        DRO dro(16, 68, 210, 32);
        dro.draw(0, -1, true);
        dro.draw(1, -1, true);
        dro.draw(2, -1, true);

        int y = 170;
        if (state == Cycle || state == Hold)
        {
            int width = 192;
            int height = 10;
            if (myPercent > 0)
            {
                drawRect(20, y, width, height, 5, LIGHTGREY);
                width = (width * myPercent) / 100;
                if (width > 0)
                {
                    drawRect(20, y, width, height, 5, GREEN);
                }
            }
            // Feed override
            char legend[50];
            if (_show_fro)
            {
                sprintf(legend, "Feed Rate Ovr:%d%%", myFro);
            }
            else
            {
                sprintf(legend, "Spindle Ovr:%d%%", mySro);
            }

            centered_text(legend, y + 23);
        }
        else
        {
            centered_text(mode_string(), y + 23, GREEN, TINY);
        }

        const char *encoder_button_text = "Menu";

        switch (state)
        {
        case Alarm:
            if (lastAlarm == 14)
            {
                redLabel = "Unlock";
            }
            else
            {
                redLabel = "Reset";
            }
            grnLabel = "Home All";
            break;
        case Homing:
            redLabel = "Reset";
            break;
        case Cycle:
            redLabel = "E-Stop";
            grnLabel = "Hold";
            break;
        case Hold:
            redLabel = "Quit";
            grnLabel = "Resume";
            break;
        case Jog:
            redLabel = "Jog Cancel";
            break;
        case Idle:
            break;
        }
        if (state == Cycle || state == Hold)
        {
            drawButtonLegends(redLabel, grnLabel, (state == Cycle || state == Hold) ? "Rst Ovr" : "Back");
        }

        refreshDisplay();
    }
};
StatusScene statusScene;
