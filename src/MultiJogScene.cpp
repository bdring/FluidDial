// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"

#include "Scene.h"
#include "ConfirmScene.h"
#include "e4math.h"

extern Scene helpScene;
extern Scene fileSelectScene;

static const char* jog_help_text[] = { "Jog Help",
                                       "Touch:",
                                       "Top/Bottom: choose axis",
                                       "Left/Right: set digit"
                                       "",
                                       "Turn: jog by digit",
                                       "Red/Grn: hold to jog",
                                       "Swipe left to exit",
                                       NULL };

class MultiJogScene : public Scene {
private:
    int          _dist_index[3] = { 2, 2, 2 };
    int          max_index() { return 6; }  // 10^3 = 1000;
    int          min_index() { return 0; }  // 10^3 = 1000;
    int          _selected_mask = 1 << 0;
    const int    num_axes       = 3;
    bool         _cancelling    = false;
    bool         _cancel_held   = false;
    bool         _continuous    = false;
    LGFX_Sprite* _bg_image      = nullptr;

public:
    MultiJogScene() : Scene("Jog", 4, jog_help_text) {}

    e4_t distance(int axis) { return e4_power10(_dist_index[axis] - num_digits()); }
    void unselect_all() { _selected_mask = 0; }
    bool selected(int axis) { return _selected_mask & (1 << axis); }
    bool only(int axis) { return _selected_mask == (1 << axis); }

    int  next(int axis) { return (axis < 2) ? axis + 1 : 0; }
    void select(int axis) { _selected_mask |= 1 << axis; }
    void unselect(int axis) { _selected_mask &= ~(1 << axis); }

    int the_selected_axis() {
        if ((_selected_mask & (_selected_mask - 1)) != 0) {
            return -2;  // Multiple axes are selected
        }
        for (size_t axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                return axis;
            }
        }
        return -1;  // No axis is selected
    }

    void reDisplay() {
        background();
        drawBackground(_bg_image);
        drawMenuTitle(current_scene->name());
        drawStatus();

        if (state != Jog && _cancelling) {
            _cancelling = false;
        }
        if (_cancelling || _cancel_held) {
            centered_text("Jog Canceled", 120, RED, MEDIUM);
        } else {
            DRO dro(16, 68, 210, 32);
            for (size_t axis = 0; axis < num_axes; axis++) {
                dro.draw(axis, _dist_index[axis], selected(axis));
            }
            if (state == Jog) {
                if (!_continuous) {
                    centered_text("Touch to cancel jog", 185, YELLOW, TINY);
                }
            } else {
                std::string dialLegend("Zero");
                for (int axis = 0; axis < num_axes; axis++) {
                    if (selected(axis)) {
                        dialLegend += axisNumToChar(axis);
                    }
                }
                drawButtonLegends("Jog-", "Jog+", dialLegend.c_str());
            }
        }
        refreshDisplay();
    }
    void zero_axes() {
        std::string cmd = "G10L20P0";
        for (int axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                cmd += axisNumToChar(axis);
                cmd += "0";
            }
        }
        send_line(cmd.c_str());
    }
    void onEntry(void* arg) {
        if (arg && strcmp((const char*)arg, "Confirmed") == 0) {
            zero_axes();
        }
        if (initPrefs()) {
            _bg_image = createPngBackground("/jogbg.png");
            for (size_t axis = 0; axis < 3; axis++) {
                getPref("DistanceDigit", axis, &_dist_index[axis]);
            }
        }
    }

    int which(int x, int y) {
        if (y > 130) {
            return 2;
        }
        return y > 90 ? 1 : 0;
    }

    void confirm_zero_axes() {
        std::string confirmMsg("Zero ");

        for (int axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                confirmMsg += axisNumToChar(axis);
            }
        }
        confirmMsg += " ?";
        dbg_println(confirmMsg.c_str());
        push_scene(&confirmScene, (void*)confirmMsg.c_str());
    }
    void set_dist_index(int axis, int value) {
        _dist_index[axis] = value;
        setPref("DistanceDigit", axis, value);
    }

    void increment_distance(int axis) {
        if (_dist_index[axis] < max_index()) {
            set_dist_index(axis, _dist_index[axis] + 1);
        }
    }
    void increment_distance() {
        for (int axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                increment_distance(axis);
            }
        }
    }
    void decrement_distance(int axis) {
        if (_dist_index[axis] > min_index()) {
            set_dist_index(axis, _dist_index[axis] - 1);
        }
    }

    void decrement_distance() {
        for (int axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                decrement_distance(axis);
            }
        }
    }
    void rotate_distance() {
        for (int axis = 0; axis < num_axes; axis++) {
            if (selected(axis)) {
                if (++_dist_index[axis] >= max_index()) {
                    _dist_index[axis] = min_index();
                }
            }
        }
    }
    void cancel_jog() {
        if (state == Jog) {
            fnc_realtime(JogCancel);
            _continuous = false;
            _cancelling = true;
        }
    }
    void next_axis() {
        int the_axis = the_selected_axis();
        if (the_axis == -2) {
            unselect_all();
            select(num_axes - 1);
            return;
        }
        if (the_axis == -1) {
            select(num_axes - 1);
            return;
        }
        unselect(the_axis);
        if (++the_axis == num_axes) {
            the_axis = 0;
        }
        select(the_axis);
    }
    void prev_axis() {
        int the_axis = the_selected_axis();
        if (the_axis == -2) {
            unselect_all();
            select(0);
            return;
        }
        if (the_axis == -1) {
            select(0);
            return;
        }
        unselect(the_axis);
        if (--the_axis < 0) {
            the_axis = num_axes - 1;
        }
        select(the_axis);
    }
    void touch_top() {
        prev_axis();
        reDisplay();
    }
    void touch_bottom() {
        next_axis();
        reDisplay();
    }
    void touch_left() {
        increment_distance();
        reDisplay();
    }
    void touch_right() {
        decrement_distance();
        reDisplay();
    }

    void onTouchPress() {
        if (state == Jog) {
            _cancel_held = true;
            cancel_jog();
        }
        reDisplay();
    }

    void onTouchRelease() {
        _cancel_held = false;
        reDisplay();
    }

    void onTouchClick() {
        if (state == Jog || _cancelling || _cancel_held) {
            return;
        }
        if (touchIsCenter()) {
            push_scene(&helpScene, (void*)_help_text);
            return;
        }

        // Convert from screen coordinates to 0,0 in the center
        Point ctr = Point { touchX, touchY }.from_display();

        int x = ctr.x;
        int y = ctr.y;

        int center_radius = display_short_side() / 6;

        // Sense touches at top, bottom, left, and right
        if (std::abs(y) > std::abs(x)) {
            if (y > 0) {
                touch_top();
            } else {
                touch_bottom();
            }
        } else {
            if (x > 0) {
                touch_right();
            } else {
                touch_left();
            }
        }
    }
    void onTouchHold() {
        // Select multiple axes
        if (touchX < 80) {
            int axis = which(touchX, touchY);
            if (selected(axis) && !only(axis)) {
                unselect(axis);
            } else {
                select(axis);
            }
            reDisplay();
            return;
        }
#if 0
        if (touchX < 160 && touchY > 160) {
            confirm_zero_axes();
        }
#endif
    }

    void onRightFlick() override {
        activate_scene(&fileSelectScene);
    }

    void onDialButtonPress() {
        zero_axes();
    }

    void start_mpg_jog(int delta) {
        // e.g. $J=G91F1000X-10000
        std::string cmd(inInches ? "$J=G91F400" : "$J=G91F10000");
        for (int axis = 0; axis < num_axes; ++axis) {
            if (selected(axis)) {
                cmd += axisNumToChar(axis);
                cmd += e4_to_cstr(delta * distance(axis), inInches ? 3 : 2);
            }
        }
        send_line(cmd.c_str());
    }
    void start_button_jog(bool negative) {
        // e.g. $J=G91F1000X-10000
        e4_t total_distance = 0;
        int  n_axes         = 0;
        for (int axis = 0; axis < num_axes; ++axis) {
            if (selected(axis)) {
                total_distance = e4_magnitude(total_distance, distance(axis));
                ++n_axes;
            }
        }

        e4_t feedrate = total_distance * 300;  // go 5x the highlighted distance in 1 second

        std::string cmd("$J=G91");
        cmd += inInches ? "G20" : "G21";
        cmd += "F";
        cmd += e4_to_cstr(feedrate, 3);
        for (int axis = 0; axis < num_axes; ++axis) {
            if (selected(axis)) {
                e4_t axis_distance;
                if (n_axes == 1) {
                    axis_distance = e4_from_int(inInches ? 200 : 5000);
                } else {
                    axis_distance = distance(axis) * 20;
                }
                if (negative) {
                    axis_distance = -axis_distance;
                }
                cmd += axisNumToChar(axis);
                cmd += e4_to_cstr(axis_distance, 0);
            }
        }
        send_line(cmd.c_str());
        _continuous = true;
    }

    void onGreenButtonPress() {
        if (state == Idle) {
            start_button_jog(false);
        }
    }
    void onGreenButtonRelease() {
        cancel_jog();
    }
    void onRedButtonPress() {
        if (state == Idle) {
            start_button_jog(true);
        }
    }
    void onRedButtonRelease() {
        cancel_jog();
    }

    void onEncoder(int delta) {
        if (delta != 0) {
            start_mpg_jog(delta);
        }
    }

    void onDROChange() {
        reDisplay();
    }
    void onLimitsChange() {
        reDisplay();
    }
    void onAlarm() {
        reDisplay();
    }
    void onExit() {
        cancel_jog();
    }
} multiJogScene;
