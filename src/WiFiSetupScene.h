#pragma once

#ifdef ARDUINO

#include "Scene.h"

class WiFiSetupScene : public Scene {
public:
    WiFiSetupScene() : Scene("WiFi") {}

    void onEntry(void* arg = nullptr) override;
    void onRedButtonPress() override;    // Start / stop AP setup
    void onGreenButtonPress() override;  // Restart ESP
    void onDialButtonPress() override;   // Back to main menu
    void onTouchClick() override;        // Same as red button
    void onStateChange(state_t) override;
    void reDisplay() override;

private:
    void drawConnectedView();
    void drawApView();
};

extern WiFiSetupScene wifiSetupScene;

#endif  // ARDUINO
