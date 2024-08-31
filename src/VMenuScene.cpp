#include "VMenu.h"
#include "System.h"

const int buttonRadius = 30;

extern Area vmenu_area;

constexpr int LIGHTYELLOW = 0xFFF0;
class IB : public ImageButton {
public:
    //    IB(const char* text, callback_t callback, const char* filename) :
    // ImageButton(text, &vmenu_area, callback, filename, buttonRadius, WHITE) {}
    IB(const char* text, Scene* scene, const char* filename) : ImageButton(text, scene, filename, buttonRadius, WHITE) {
        _highlighted = false, _disabled = false;
    }
};

extern Scene homingScene;
//extern Scene joggingScene;
//extern Scene joggingScene2;
extern Scene multiJogScene;
extern Scene probingScene;
extern Scene statusScene;
extern Scene macroMenu;

#ifdef USE_WMB_FSS
extern Scene wmbFileSelectScene;
#else
extern Scene fileSelectScene;
#endif

Scene& jogScene = multiJogScene;

extern Scene controlScene;
extern Scene aboutScene;
extern Scene powerScene;

IB statusButton("Status", &statusScene, "statustp.png");
IB homingButton("Homing", &homingScene, "hometp.png");
IB jogButton("Jog", &jogScene, "jogtp.png");
IB probeButton("Probe", &probingScene, "probetp.png");

#ifdef USE_WMB_FSS
IB filesButton("Files", &wmbFileSelectScene, "filestp.png");
#else
IB           filesButton("Files", &fileSelectScene, "filestp.png");
#endif

IB controlButton("Macros", &macroMenu, "macrostp.png");
IB setupButton("About", &aboutScene, "abouttp.png");
IB powerButton("Power", &powerScene, "powertp.png");

Scene& menuScene = statusScene;

VMenu       vmenu(&vmenu_area);
extern Area xscene_area0;
extern Area xscene_area1;
extern Area xscene_area2;
extern Area xscene_area3;
extern Area xscene_area4;
extern Area xscene_area5;

Scene* initMenus() {
    statusScene.set_area(&xscene_area0);
    homingScene.set_area(&xscene_area1);
    probingScene.set_area(&xscene_area2);
    jogScene.set_area(&xscene_area3);
    macroMenu.set_area(&xscene_area4);
    fileSelectScene.set_area(&xscene_area5);

    vmenu.addItem(&statusButton);
    vmenu.addItem(&homingButton);
    vmenu.addItem(&jogButton);
    vmenu.addItem(&probeButton);
    vmenu.addItem(&filesButton);
    vmenu.addItem(&controlButton);
    vmenu.addItem(&setupButton);
    vmenu.addItem(&powerButton);
    vmenu.reDisplay();

    return &statusScene;
}
