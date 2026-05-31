#pragma once

#ifdef USE_WIFI

#include "Scene.h"

class TransportScene : public Scene {
    int _selected = 0;

public:
    TransportScene() : Scene("Transport", 4) {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onGreenButtonPress() override;
    void onRedButtonPress() override;
    void onTouchClick() override;
    void reDisplay() override;

private:
    void confirmSelection();
};

extern TransportScene transportScene;

#endif
