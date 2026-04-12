#pragma once

#ifdef ARDUINO

#include "Scene.h"

class WiFiSetupScene : public Scene {
public:
    WiFiSetupScene() : Scene("Settings") {}

    void onEntry(void* arg = nullptr) override;
    void onRedButtonPress() override;    // Switch transport (or stop AP)
    void onGreenButtonPress() override;  // AP Setup / Restart
    void onDialButtonPress() override;   // Back to menu
    void onTouchClick() override;        // Same as Red
    void onStateChange(state_t) override;
    void reDisplay() override;

private:
    void switchModeAndRestart();
    void drawSettingsView();
    void drawApView();
};

extern WiFiSetupScene wifiSetupScene;

#endif  // ARDUINO
