// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "System.h"
#include "alarm.h"

#ifndef ARDUINO
#    include <sys/stat.h>
#    include <sys/types.h>
#endif
#include <vector>
#include <map>

extern Scene homingScene;
extern Scene statusScene;

Scene* current_scene = nullptr;

int touchX;
int touchY;
int touchDeltaX;
int touchDeltaY;

std::vector<Scene*> scene_stack;

void activate_scene(Scene* scene, void* arg) {
    if (current_scene) {
        current_scene->onExit();
    }
    current_scene = scene;
    current_scene->onEntry(arg);
    current_scene->reDisplay();
}
void push_scene(Scene* scene, void* arg) {
    scene_stack.push_back(current_scene);
    activate_scene(scene, arg);
}
void pop_scene(void* arg) {
    if (scene_stack.size()) {
        Scene* last_scene = scene_stack.back();
        scene_stack.pop_back();
        activate_scene(last_scene, arg);
    }
}
void activate_at_top_level(Scene* scene, void* arg) {
    scene_stack.clear();
    activate_scene(scene, arg);
}
Scene* parent_scene() {
    return scene_stack.size() ? scene_stack.back() : nullptr;
}

bool Scene::touchIsCenter() {
    // Convert from screen coordinates to 0,0 in the center
    Point ctr = area()->from_display(Point { touchX, touchY });

    int center_radius = area()->w() / 6;

    return (ctr.x * ctr.x + ctr.y * ctr.y) < (center_radius * center_radius);
}

void dispatch_button(bool pressed, int button) {
    switch (button) {
        case 0:
            if (pressed) {
                current_scene->onRedButtonPress();
            } else {
                current_scene->onRedButtonRelease();
            }
            break;
        case 1:
            if (pressed) {
                current_scene->onDialButtonPress();
            } else {
                current_scene->onDialButtonRelease();
            }
            break;
        case 2:
            if (pressed) {
                current_scene->onGreenButtonPress();
            } else {
                current_scene->onGreenButtonRelease();
            }
            break;
        default:
            break;
    }
}
bool auxiliary_touch(int x, int y);

void dispatch_touch() {
    static m5::touch_state_t last_touch_state = {};

    auto t = touch.getDetail();
    if (t.state != last_touch_state) {
        last_touch_state = t.state;
        int delta;
        if (screen_encoder(t.x, t.y, delta) && t.state == m5::touch_state_t::touch) {
            current_scene->onEncoder(delta);
            return;
        }
        if (t.state == m5::touch_state_t::touch && auxiliary_touch(t.x, t.y)) {
            return;
        }
        int button;
        if (screen_button_touched(t.state == m5::touch_state_t::touch, t.x, t.y, button)) {
            if (t.state == m5::touch_state_t::touch) {
                dispatch_button(true, button);
            } else if (t.state == m5::touch_state_t::none) {
                dispatch_button(false, button);
            }
            return;
        }
        touchX = t.x - scene_area->x();
        touchY = t.y - scene_area->y();
        if (touchX < 0) {
            return;
        }
        if (t.state == m5::touch_state_t::touch) {
            current_scene->onTouchPress();
        } else if (t.state == m5::touch_state_t::none) {
            current_scene->onTouchRelease();
        }
        if (t.wasClicked()) {
            current_scene->onTouchClick();
        } else if (t.wasHold()) {
            current_scene->onTouchHold();
        } else if (t.state == m5::touch_state_t::flick_end) {
            touchDeltaX = t.distanceX();
            touchDeltaY = t.distanceY();

            int absX = abs(touchDeltaX);
            int absY = abs(touchDeltaY);
            if (absY > 60 && absX < (absY * 2)) {
                if (touchDeltaY > 0) {
                    current_scene->onDownFlick();
                } else {
                    current_scene->onUpFlick();
                }
            } else if (absX > 60 && absY < (absX * 2)) {
                if (touchDeltaX > 0) {
                    current_scene->onRightFlick();
                } else {
                    current_scene->onLeftFlick();
                }
            } else {
                current_scene->onTouchFlick();
            }
        }
    }
}

ActionHandler action = nullptr;

void schedule_action(ActionHandler _action) {
    action = _action;
}

void dispatch_events() {
    update_events();

    static int16_t oldEncoder   = 0;
    int16_t        newEncoder   = get_encoder();
    int16_t        encoderDelta = newEncoder - oldEncoder;
    if (encoderDelta) {
        oldEncoder = newEncoder;

        int16_t scaledDelta = current_scene->scale_encoder(encoderDelta);
        if (scaledDelta) {
            current_scene->onEncoder(scaledDelta);
        }
    }

    bool pressed;
    int  button;
    if (switch_button_touched(pressed, button)) {
        dispatch_button(pressed, button);
    }

    dispatch_touch();

    if (!fnc_is_connected()) {
        if (state != Disconnected) {
            set_disconnected_state();
            extern Scene menuScene;
            //            activate_at_top_level(&menuScene);
        }
    }

    if (action) {
        action();
        action = nullptr;
    }
}

static const char* setting_name(const char* base_name, int axis) {
    static char name[32];
    if (axis == -1) {
        return base_name;
    }
    sprintf(name, "%s%c", base_name, axisNumToChar(axis));
    return name;
}

void Scene::setPref(const char* name, int value) {
    setPref(name, -1, value);
}
void Scene::getPref(const char* name, int* value) {
    getPref(name, -1, value);
}

void Scene::setPref(const char* base_name, int axis, int value) {
    if (!_prefs) {
        return;
    }
    nvs_set_i32(_prefs, setting_name(base_name, axis), value);
}
void Scene::getPref(const char* base_name, int axis, int* value) {
    if (!_prefs) {
        return;
    }
    nvs_get_i32(_prefs, setting_name(base_name, axis), value);
}
void Scene::setPref(const char* base_name, int axis, const char* value) {
    if (!_prefs) {
        return;
    }
    nvs_set_str(_prefs, setting_name(base_name, axis), value);
}
void Scene::getPref(const char* base_name, int axis, char* value, int maxlen) {
    if (!_prefs) {
        return;
    }
    size_t len = maxlen;
    nvs_get_str(_prefs, setting_name(base_name, axis), value, &len);
}
bool Scene::initPrefs() {
    if (_prefs) {
        return false;  // Already open
    }
    _prefs = nvs_init(name());
    return _prefs;
}

int Scene::scale_encoder(int delta) {
    _encoder_accum += delta;
    int res = _encoder_accum / _encoder_scale;
    _encoder_accum %= _encoder_scale;
    return res;
}

void Scene::background() {
    system_background(area());
}

void act_on_state_change() {
    current_scene->onStateChange(previous_state);
}

#define PUSH_BUTTON_LINE 212
#define DIAL_BUTTON_LINE 228

static int side_button_line() {
    return round_display ? PUSH_BUTTON_LINE : DIAL_BUTTON_LINE;
}

void Scene::title() {
    centered_text(name(), 12, WHITE);
}

// This shows on the display what the button currently do.
void Scene::buttonLegends(const char* red, const char* green, const char* orange) {
    text(red, round_display ? 50 : 10, side_button_line(), RED, TINY, middle_left);
    text(green, area()->w() - (round_display ? 50 : 10), side_button_line(), GREEN, TINY, middle_right);
    centered_text(orange, DIAL_BUTTON_LINE, ORANGE);
}

void Scene::showError() {
    if (lastError) {
        if ((milliseconds() - errorExpire) < 0) {
            area()->drawFilledCircle(Point { 0, 0 }, 95, RED);
            area()->drawCircle(Point { 0, 0 }, 95, 5, WHITE);
            centered_text("Error", { 0, -25 }, WHITE, MEDIUM);
            centered_text(decode_error_number(lastError), { 0, 10 }, WHITE, TINY);
        } else {
            lastError = 0;
        }
    }
}

// clang-format off
// We use 1 to mean no background
// 1 is visually indistinguishable from black so losing that value is unimportant
#define NO_BG 1

std::map<state_t, int> stateBGColors = {
    { Idle,         NO_BG },
    { Alarm,        RED },
    { CheckMode,    WHITE },
    { Homing,       NO_BG },
    { Cycle,        NO_BG },
    { Hold,         YELLOW },
    { Jog,          NO_BG },
    { DoorOpen,     RED },
    { DoorClosed,   YELLOW },
    { GrblSleep,    WHITE },
    { ConfigAlarm,  WHITE },
    { Critical,     WHITE },
    { Disconnected, RED },
};
std::map<state_t, int> stateFGColors = {
    { Idle,         LIGHTGREY },
    { Alarm,        BLACK },
    { CheckMode,    BLACK },
    { Homing,       CYAN },
    { Cycle,        GREEN },
    { Hold,         BLACK },
    { Jog,          CYAN },
    { DoorOpen,     BLACK },
    { DoorClosed,   BLACK },
    { GrblSleep,    BLACK },
    { ConfigAlarm,  BLACK },
    { Critical,     BLACK },
    { Disconnected, BLACK },
};
// clang-format on

void Scene::status() {
    static constexpr int x      = 100;
    static constexpr int y      = 24;
    static constexpr int width  = 140;
    static constexpr int height = 36;

    int bgColor = stateBGColors[state];
    if (bgColor != 1) {
        area()->drawRect((area()->w() - width) / 2, y, width, height, 5, bgColor);
    }
    int fgColor = stateFGColors[state];
    if (state == Alarm) {
        centered_text(my_state_string, y + height / 2 - 4, fgColor, SMALL);
        centered_text(alarm_name_short[lastAlarm], y + height / 2 + 12, fgColor);
    } else {
        centered_text(my_state_string, y + height / 2 + 3, fgColor, MEDIUM);
    }
}

void Scene::statusTiny(int y) {
    static constexpr int width  = 90;
    static constexpr int height = 20;

    int bgColor = stateBGColors[state];
    if (bgColor != 1) {
        area()->drawRect((area()->w() - width) / 2, y, width, height, 5, bgColor);
    }
    centered_text(my_state_string, y + height / 2 + 3, stateFGColors[state], TINY);
}

void Scene::statusSmall(int y) {
    static constexpr int width  = 90;
    static constexpr int height = 25;

    int bgColor = stateBGColors[state];
    if (bgColor != 1) {
        area()->drawRect((area()->w() - width) / 2, y, width, height, 5, bgColor);
    }
    centered_text(my_state_string, y + height / 2 + 3, stateFGColors[state], SMALL);
}

Area* scene_area;
