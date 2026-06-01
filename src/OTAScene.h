#pragma once

#ifdef USE_WIFI

#include "Scene.h"

class OTAScene : public Scene {
public:
    OTAScene() : Scene("OTA Update") {}

    void onEntry(void* arg = nullptr) override;
    void onRedButtonPress() override;
    void onGreenButtonPress() override;
    void reDisplay() override;
};

extern OTAScene otaScene;

#endif
