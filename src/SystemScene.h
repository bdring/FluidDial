#pragma once

#ifdef USE_WIFI

#include "Scene.h"

class SystemScene : public Scene {
    int _selected = 0;

public:
    SystemScene() : Scene("System") {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onDialButtonPress() override;
    void onGreenButtonPress() override;
    void onRedButtonPress() override;
    void onTouchClick() override;
    void reDisplay() override;

private:
    void activateSelected();
    static int itemCount();
};

extern SystemScene systemScene;

#endif  // USE_WIFI
