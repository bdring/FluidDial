#include "Menu.h"
#include <string>

// Confirm scene needs a string to display.  It displays the string on
// a background depicting the red and green buttons.
// It pops when one of those buttons is pressed, or on back and flick back,
// setting a variable to true iff the green button was pressed

class ConfirmScene : public Scene {
    std::string _msg;

public:
    ConfirmScene() : Scene("Confirm") {}
    void onEntry(void* arg) { _msg = (const char*)arg; }
    void reDisplay() {
        background();
        canvas.fillRoundRect(10, 90, 220, 60, 15, YELLOW);
        centered_text(_msg.c_str(), 120, BLACK, MEDIUM);

        drawButtonLegends("No", "Yes", "Back");

        refreshDisplay();
    }
    void onRedButtonPress() { pop_scene(nullptr); }
    void onGreenButtonPress() { pop_scene((void*)"Confirmed"); }
    void onDialButtonPress() { pop_scene(nullptr); }
};
ConfirmScene confirmScene;
