#include "Menu.h"
#include <string>

class ConfirmScene : public Scene {
    std::string _msg;

public:
    ConfirmScene() : Scene("Confirm") {}
    void onEntry(void* arg) { _msg = (const char*)arg; }
    void reDisplay() {
        background();
        canvas.fillRoundRect(10, 90, 220, 60, 15, YELLOW);

        size_t nl = _msg.find('\n');
        if (nl != std::string::npos) {
            centered_text(_msg.substr(0, nl).c_str(), 108, BLACK, MEDIUM);
            centered_text(_msg.substr(nl + 1).c_str(), 132, BLACK, MEDIUM);
        } else {
            centered_text(_msg.c_str(), 120, BLACK, MEDIUM);
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
