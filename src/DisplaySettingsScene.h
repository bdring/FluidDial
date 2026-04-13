#pragma once

#ifdef ARDUINO

#include "Scene.h"

class DisplaySettingsScene : public Scene {
public:
    DisplaySettingsScene() : Scene("Display") {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onRedButtonPress() override;
    void reDisplay() override;
};

extern DisplaySettingsScene displaySettingsScene;

#endif  // ARDUINO
