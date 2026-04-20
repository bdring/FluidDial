#pragma once

#ifdef USE_M5

#include "Scene.h"

class BrightnessScene : public Scene {
    int _brightness = 255;

public:
    BrightnessScene() : Scene("Brightness") {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onRedButtonPress() override;
    void reDisplay() override;
    int  getBrightness();
};

extern BrightnessScene brightnessScene;

#endif  // USE_M5
