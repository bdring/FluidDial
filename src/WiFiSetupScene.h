#pragma once

#ifdef ARDUINO

#include "Scene.h"
#include "Button.h"

class WiFiSetupScene : public Scene {
public:
    WiFiSetupScene() : Scene("Settings") {}

    void onEntry(void* arg = nullptr) override;
    void onRedButtonPress() override;    // Switch transport (or stop AP)
    void onGreenButtonPress() override;  // AP Setup / Restart
    void onDialButtonPress() override;   // Display settings
    void onTouchClick() override;        // Interactive button
    void onStateChange(state_t) override;
    void reDisplay() override;

private:
    void switchModeAndRestart();
    void onModeSwitchButtonPress();
    void drawSettingsView();
    void drawApView();
    Button modeSwitchBtn;
};

extern WiFiSetupScene wifiSetupScene;

#endif  // ARDUINO
