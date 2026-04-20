#include "Menu.h"
#include <string>

class ConfirmScene : public Scene {
    std::string _msg;

public:
    ConfirmScene() : Scene("Confirm") {}
    void onEntry(void* arg) { _msg = (const char*)arg; }
    void reDisplay() {
        background();

        if (!round_display) {
            drawOutlinedRect(10, 80, 230, 100, 0x001a4d, 0x4da6ff);
        }
        size_t nl = _msg.find('\n');
        if (nl != std::string::npos) {
            centered_text(_msg.substr(0, nl).c_str(), 108, 0x4da6ff, SMALL);
            centered_text(_msg.substr(nl + 1).c_str(), 132, 0x4da6ff, SMALL);
        } else {
            centered_text(_msg.c_str(), 120, 0x4da6ff, MEDIUM);
        }

        drawButtonLegends("No", "Yes", "Back");

        refreshDisplay();
    }
    void onRedButtonPress() { pop_scene(nullptr); }
    void onGreenButtonPress() {
        dbg_printf("ConfirmScene: Yes pressed\r\n");
        pop_scene((void*)"Confirmed");
    }
    void onDialButtonPress() { pop_scene(nullptr); }
};
ConfirmScene confirmScene;
