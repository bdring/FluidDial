#pragma once

#include "Scene.h"

class BrightnessScene : public Scene {
    int _brightness = 100;  // stored as percentage 0-100

public:
    BrightnessScene() : Scene("Brightness", 4) {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onRedButtonPress() override;
    void reDisplay() override;
    int  getBrightness();
};

extern BrightnessScene brightnessScene;
