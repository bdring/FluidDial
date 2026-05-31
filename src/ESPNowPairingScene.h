#pragma once
#ifdef USE_WIFI

#include "Scene.h"

class ESPNowPairingScene : public Scene {
    Scene* _fallback = nullptr;
public:
    ESPNowPairingScene() : Scene("Pair") {}

    void onEntry(void* arg = nullptr) override;
    void onExit() override;
    void onRedButtonPress() override;
    void onPoll() override;
    void reDisplay() override;
};

extern ESPNowPairingScene espnowPairingScene;

#endif
