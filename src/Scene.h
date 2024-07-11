// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "GrblParserC.h"
#include "Drawing.h"
#include "NVS.h"
#include <vector>

void pop_scene(void* arg = nullptr);

extern int touchX;
extern int touchY;
extern int touchDeltaX;
extern int touchDeltaY;

class Scene {
private:
    const char* _name;

    nvs_handle_t _prefs {};

    int _encoder_accum = 0;
    int _encoder_scale = 1;

protected:
    const char** _help_text = nullptr;

public:
    Scene(const char* name, int encoder_scale = 1, const char** help_text = nullptr) :
        _name(name), _help_text(help_text), _encoder_scale(encoder_scale) {}

    const char* name() { return _name; }

    virtual void onRedButtonPress() {}
    virtual void onRedButtonRelease() {}
    virtual void onGreenButtonPress() {}
    virtual void onGreenButtonRelease() {}
    virtual void onDialButtonPress() {}
    virtual void onDialButtonRelease() {}
    virtual void onTouchPress() {}
    virtual void onTouchRelease() {}
    virtual void onTouchClick() {}
    virtual void onTouchHold() {}
    virtual void onLeftFlick() { pop_scene(); }
    virtual void onRightFlick() { onTouchFlick(); }
    virtual void onUpFlick() { onTouchFlick(); }
    virtual void onDownFlick() { onTouchFlick(); }
    virtual void onTouchFlick() {}

    virtual void onError(const char* errstr) {}

    virtual void onStateChange(state_t) {}
    virtual void onDROChange() {}
    virtual void onLimitsChange() {}
    virtual void onMessage(char* command, char* arguments) {}
    virtual void onEncoder(int delta) {}
    virtual void reDisplay() {}
    virtual void onEntry(void* arg = nullptr) {}
    virtual void onExit() {}

    virtual void onFileLines(int firstline, const std::vector<std::string>& lines) {}
    virtual void onFilesList() {}

    bool initPrefs();

    int scale_encoder(int delta);

    void setPref(const char* name, int value);
    void getPref(const char* name, int* value);
    void setPref(const char* name, int axis, int value);
    void getPref(const char* name, int axis, int* value);
    void setPref(const char* name, int axis, const char* value);
    void getPref(const char* name, int axis, char* value, int maxlen);

    void background();
};

bool touchIsCenter();

void   activate_at_top_level(Scene* scene, void* arg = nullptr);
void   activate_scene(Scene* scene, void* arg = nullptr);
void   push_scene(Scene* scene, void* arg = nullptr);
Scene* parent_scene();

// helper functions

// Function to rotate through an aaray of numbers
// Example:  rotateNumberLoop(variable, 1, 0, 2)
// The variable can be integer or float.  If it is float, you need
// to cast increment, min, and max to float in the calls, otherwise
// the compiler will try to use double and complain

template <typename T>
void rotateNumberLoop(T& currentVal, T increment, T min, T max) {
    currentVal += increment;
    if (currentVal > max) {
        currentVal = min;
    }
    if (currentVal < min) {
        currentVal = max;
    }
}

// schedule_action() defers a function call until the
// event dispatcher loop runs.  That is useful for
// avoiding recursion in FileParser.cpp
typedef void (*ActionHandler)(void);
void schedule_action(ActionHandler action);

extern Scene* current_scene;

void dispatch_events();
void act_on_state_change();
