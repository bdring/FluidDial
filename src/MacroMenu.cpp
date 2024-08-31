// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Menu.h"
#include "MacroItem.h"
#include "polar.h"
#include "FileParser.h"

extern Scene statusScene;
extern Scene filePreviewScene;

void MacroItem::invoke(void* arg) {
    if (arg && strcmp((char*)arg, "Run") == 0) {
        send_linef("$Localfs/Run=%s", _filename.c_str());
    } else {
        push_scene(&filePreviewScene, (void*)_filename.c_str());
        // doFileScreen(_name);
    }
}
void MacroItem::show(Area* area) {
    int color = WHITE;

    std::string extra(_filename);
    if (extra.rfind("/localfs", 0) == 0) {
        extra.erase(0, strlen("/localfs"));
    } else if (extra.rfind("cmd:", 0) == 0) {
        extra.erase(0, strlen("cmd:"));
    }

    if (_highlighted) {
        area->drawRect(_position, Point { 200, 50 }, 15, color);
        area->text(name(), _position + Point { 0, 6 }, BLACK, MEDIUM, middle_center);
        area->text(extra, _position - Point { 0, 16 }, BLACK, TINY, middle_center);
    } else {
        area->text(name(), _position, WHITE, SMALL, middle_center);
    }
}

class MacroMenu : public Menu {
private:
    bool        _reading = true;
    std::string _error_string;

public:
    MacroMenu() : Menu("Macros") {}

    const std::string& selected_name() { return _items[_selected]->name(); }

    void refreshMacros() {
        removeAllItems();
        _reading = true;
        request_macros();
    }

    void onRedButtonPress() { refreshMacros(); }
    void onFilesList() {
        _error_string.clear();
        _reading = false;
        if (num_items()) {
            _selected = 0;
            _items[_selected]->highlight();
        }
        reDisplay();
    }

    void onError(const char* errstr) {
        _error_string = errstr;
        _reading      = false;
        reDisplay();
    }

    void onEntry(void* arg) override {
        if (num_items() == 0) {
            refreshMacros();
        }
    }

    void onDialButtonPress() {
        if (num_items()) {
            invoke((void*)"Run");
        }
    }

    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (num_items()) {
            invoke();
        }
    }

    void onTouchClick() { onGreenButtonPress(); }

    void myButtonLegends() {
        const char* orangeLabel = "";
        const char* grnLabel    = "";

        if (state == Idle) {
            if (num_items()) {
                orangeLabel = "Run";
                grnLabel    = "Load";
            }
        }

        buttonLegends(_reading ? "" : "Refresh", grnLabel, orangeLabel);
    }

    void reDisplay() override {
        menuBackground();
        if (num_items() == 0) {
            // Point where { 0, 0 };
            // Point wh { 200, 45 };
            // drawRect(where, wh, 20, YELLOW);
            if (_error_string.length()) {
                text(_error_string, { 0, 0 }, WHITE, SMALL, middle_center);
            } else {
                text(_reading ? "Reading Macros" : "No Macros", { 0, 0 }, WHITE, SMALL, middle_center);
            }
        } else {
            if (_selected > 1) {
                _items[_selected - 2]->show(area(), { 0, 80 });
            }
            if (_selected > 0) {
                _items[_selected - 1]->show(area(), { 0, 45 });
            }
            _items[_selected]->show(area(), { 0, 0 });
            if (_selected < num_items() - 1) {
                _items[_selected + 1]->show(area(), { 0, -45 });
            }
            if (_selected < num_items() - 2) {
                _items[_selected + 2]->show(area(), { 0, -80 });
            }
        }
        myButtonLegends();
        refreshDisplay();
    }

    void rotate(int delta) override {
        if (_selected == 0 && delta <= 0) {
            return;
        }
        if (_selected == num_items() && delta >= 0) {
            return;
        }
        if (_selected != -1) {
            _items[_selected]->unhighlight();
        }
        _selected += delta;
        if (_selected < 0) {
            _selected = 0;
        }
        if (_selected >= num_items()) {
            _selected = num_items() - 1;
        }
        _items[_selected]->highlight();
        reDisplay();
    }

    int touchedItem(int x, int y) override { return -1; };

    void onStateChange(state_t old_state) {
        if (state == Cycle) {
            push_scene(&statusScene);
        }
    }

    void menuBackground() override {
        background();

        if (num_items()) {
            // Draw dot showing the selected file
            if (num_items() > 1) {
                int span   = 100;  // degrees
                int dtheta = span * _selected / (num_items() - 1);
                int theta  = (span / 2) - dtheta;
                int dx, dy;
                r_degrees_to_xy(110, theta, &dx, &dy);

                area()->drawFilledCircle({ dx, dy }, 8, WHITE);
            }
        }
        if (state != Idle) {
            status();
        }

        text("Macros", { 0, 100 }, YELLOW, SMALL);
    }
} macroMenu;
