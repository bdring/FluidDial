// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "System.h"

#ifndef ARDUINO
#    include <sys/stat.h>
#    include <sys/types.h>
#endif

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

bool touchIsCenter() {
    // Convert from screen coordinates to 0,0 in the center
    Point ctr = Point { touchX, touchY }.from_display();

    int center_radius = display.width() / 6;

    return (ctr.x * ctr.x + ctr.y * ctr.y) < (center_radius * center_radius);
}
// This handles touches that are outside the round area of the M5Dial screen.
// It is used for the PC emulation version, where the display is rectangular.
// Touches (mouse clicks) outside of the round dial screen part are used
// for emulating dial encoder motions and red/green/dial button presses.
bool outside_touch_handled(int x, int y, const m5::touch_detail_t& t) {
    x -= display.width() / 2;
    y -= display.height() / 2;
    int magsq = x * x + y * y;
    if (magsq > (120 * 120)) {
        if (y < 0) {
            if (t.state == m5::touch_state_t::touch) {
                int tangent = y * 100 / x;
                if (tangent < 0) {
                    tangent = -tangent;
                }
                int delta = 4;
                if (tangent > 172) {  // tan(60)*100
                    delta = 1;
                } else if (tangent > 100) {  // tan(45)*100
                    delta = 2;
                } else if (tangent > 58) {  // tan(30)*100
                    delta = 3;
                }
                if (t.state == m5::touch_state_t::touch) {
                    current_scene->onEncoder((x > 0) ? delta : -delta);
                }
            }
        } else {
            if (x <= -90) {
                if (t.state == m5::touch_state_t::touch) {
                    current_scene->onRedButtonPress();
                } else if (t.state == m5::touch_state_t::none) {
                    current_scene->onRedButtonRelease();
                }
            } else if (x >= 90) {
                if (t.state == m5::touch_state_t::touch) {
                    current_scene->onGreenButtonPress();
                } else if (t.state == m5::touch_state_t::none) {
                    current_scene->onGreenButtonRelease();
                }
            } else {
                if (t.state == m5::touch_state_t::touch) {
                    current_scene->onDialButtonPress();
                } else if (t.state == m5::touch_state_t::none) {
                    current_scene->onDialButtonRelease();
                }
            }
        }
        return true;
    }
    return false;
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

    if (dialButton.wasPressed()) {
        current_scene->onDialButtonPress();
    } else if (dialButton.wasReleased()) {
        current_scene->onDialButtonRelease();
    }
    if (redButton.wasPressed()) {
        current_scene->onRedButtonPress();
    } else if (redButton.wasReleased()) {
        current_scene->onRedButtonRelease();
    }
    if (greenButton.wasPressed()) {
        current_scene->onGreenButtonPress();
    } else if (greenButton.wasReleased()) {
        current_scene->onGreenButtonRelease();
    }

    static m5::touch_state_t last_touch_state = {};

    auto this_touch = touch.getDetail();
    if (this_touch.state != last_touch_state) {
        last_touch_state = this_touch.state;
        touchX           = this_touch.x;
        touchY           = this_touch.y;
        if (!outside_touch_handled(this_touch.x, this_touch.y, this_touch)) {
            if (this_touch.state == m5::touch_state_t::touch) {
                current_scene->onTouchPress();
            } else if (this_touch.state == m5::touch_state_t::none) {
                current_scene->onTouchRelease();
            }
            if (this_touch.wasClicked()) {
                current_scene->onTouchClick();
            } else if (this_touch.wasHold()) {
                current_scene->onTouchHold();
            } else if (this_touch.state == m5::touch_state_t::flick_end) {
                touchDeltaX = this_touch.distanceX();
                touchDeltaY = this_touch.distanceY();

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

    if (!fnc_is_connected()) {
        if (state != Disconnected) {
            set_disconnected_state();
            extern Scene menuScene;
            activate_at_top_level(&menuScene);
        }
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

#ifdef ARDUINO
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
    esp_err_t err = nvs_open(name(), NVS_READWRITE, &_prefs);
    return err == ESP_OK;
}
#else
const char* Scene::prefFileName(const char* pname, int axis) {
    static char fname[60];
    snprintf(fname, 60, "prefs/%s/%s", name(), setting_name(pname, axis));
    return fname;
}
void Scene::setPref(const char* base_name, int axis, int value) {
    char val[20];
    snprintf(val, 20, "%d", value);
    setPref(base_name, axis, val);
}
void Scene::setPref(const char* base_name, int axis, const char* value) {
    FILE* fd = fopen(prefFileName(base_name, axis), "wb");
    if (fd) {
        fwrite(value, 1, strlen(value), fd);
        fclose(fd);
    }
}
void Scene::getPref(const char* base_name, int axis, char* value, int maxlen) {
    FILE* fd = fopen(prefFileName(base_name, axis), "rb");
    if (fd) {
        size_t len = fread(value, 1, maxlen - 1, fd);
        value[len] = '\0';
    }
}
void Scene::getPref(const char* base_name, int axis, int* value) {
    char strval[20];
    getPref(base_name, axis, strval, 20);
    if (*strval) {
        *value = atoi(strval);
    }
}
bool Scene::initPrefs() {
    char dname[50];
    mkdir("prefs");
    snprintf(dname, 50, "prefs/%s", name());
    mkdir(dname);

    return true;
}
#endif

int Scene::scale_encoder(int delta) {
    _encoder_accum += delta;
    int res = _encoder_accum / _encoder_scale;
    _encoder_accum %= _encoder_scale;
    return res;
}

void Scene::background() {
#ifdef ARDUINO
    drawBackground(BLACK);
#else
    drawPngBackground("PCBackground.png");
#endif
}

void act_on_state_change() {
    current_scene->onStateChange(previous_state);
}
