#pragma once

#ifdef USE_WIFI

#include "Scene.h"

class ESPNowMachineScene : public Scene {
public:
    ESPNowMachineScene() : Scene("Machines", 4) {}

    void onEntry(void* arg = nullptr) override;
    void onEncoder(int delta) override;
    void onRedButtonPress() override;
    void onGreenButtonPress() override;
    void onDialButtonPress() override;
    void onTouchClick() override;
    void reDisplay() override;

private:
    int itemCount() const;
    void activateSelected();
    void confirmDeleteSelected();
    void deletePendingProfile();

    int _selected = 0;
    int _profile_count = 0;
    int _pending_delete = -1;
    char _confirm_message[80] = {};
};

extern ESPNowMachineScene espnowMachineScene;

#endif  // USE_WIFI
