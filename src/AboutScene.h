#include "Scene.h"

class AboutScene : public Scene {
private:
    int _brightness = 255;

public:
    AboutScene() : Scene("About", 4) {}

    void onEntry(void* arg);

    void onDialButtonPress();
    void onGreenButtonPress();
    void onRedButtonPress();

    void onTouchClick() override;

    void onEncoder(int delta);
    void onStateChange(state_t old_state);
    void reDisplay();
    int  getBrightness();
};
