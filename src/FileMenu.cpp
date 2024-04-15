#if 0
// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#    include "FileMenu.h"
#    include <math.h>
#    include <string>
#    include "FileParser.h"
#    include "polar.h"

extern Scene filePreviewScene;

static bool isDirectory(const std::string& name) {
    return name.length() && name[name.length() - 1] == '/';
}
static std::string dirNameOnly(const std::string& name) {
    if (isDirectory(name)) {
        return name.substr(0, name.length() - 1);
    }
    return name;
}

static std::string baseName(const std::string& name) {
    if (isDirectory(name)) {
        return name.substr(0, name.length() - 1);
    }
    auto dotpos = name.rfind('.');
    if (dotpos != std::string::npos) {
        return name.substr(0, dotpos);
    }
    return name;
}

void FileItem::show(const Point& where) {
    dbg_printf("Show %s\n", name().c_str());
    int         color = WHITE;
    std::string s     = baseName(name());
    if (isDirectory(name())) {
        color = YELLOW;
        s += " >";
    }

    if (_highlighted) {
        Point wh { 200, 45 };
        drawRect(where, wh, 20, color);
        text(s.c_str(), where, BLACK, TINY, middle_center);
    } else {
        text(s.c_str(), where, WHITE, TINY, middle_center);
    }
}

const std::string& FileMenu::selected_name() {
    return _items[_selected]->name();
}

void FileMenu::onEntry(void* arg) {
    dbg_println("Entering fss");
}

void FileMenu::onRedButtonPress() {
    if (state != Idle) {
        return;
    }
    if (dirLevel) {
        exit_directory();
    } else {
        init_file_list();
    }
    ackBeep();
}
void FileMenu::onFilesList() {
    _selected = 0;
    reDisplay();
}

void FileMenu::onDialButtonPress() {
    pop_scene();
}

void FileMenu::onGreenButtonPress() {
    if (state != Idle) {
        return;
    }
    if (num_items()) {
        std::string dName;
        if (isDirectory(selected_name())) {
            enter_directory(dirNameOnly(selected_name()).c_str());
        } else {
            push_scene(&filePreviewScene, (void*)(selected_name().c_str()));
        }
    }
    ackBeep();
}

void FileMenu::reDisplay() {
    dbg_printf("FM redisp sel %d ni %d\n", _selected, num_items());
    menuBackground();
    if (num_items() == 0) {
        Point where { 0, 0 };
        Point wh { 200, 45 };
        drawRect(where, wh, 20, YELLOW);
        text("No Files", where, BLACK, MEDIUM, middle_center);
    } else {
        if (_selected > 1) {
            _items[_selected - 2]->show({ 0, 70 });
        }
        if (_selected > 0) {
            _items[_selected - 1]->show({ 0, 40 });
        }
        dbg_println(selected_name());
        _items[_selected]->show({ 0, 0 });
        if (_selected < num_items() - 1) {
            _items[_selected + 1]->show({ 0, -40 });
        }
        if (_selected < num_items() - 2) {
            _items[_selected + 2]->show({ 0, -70 });
        }
    }
    buttonLegends();
    refreshDisplay();
}
void FileMenu::buttonLegends() {
    const char* grnLabel = "";
    const char* redLabel = "";

    if (state == Idle) {
        redLabel = dirLevel ? "Up.." : "Refresh";

        if (num_items()) {
            grnLabel = isDirectory(selected_name()) ? "Down.." : "Load";
        }
    }

    drawButtonLegends(redLabel, grnLabel, "Back");
}

void FileMenu::rotate(int delta) {
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

int FileMenu::touchedItem(int x, int y) {
    return -1;
}

void FileMenu::menuBackground() {
    background();
    drawPngBackground("/filesbg.png");

    text(dirName, { 0, 100 }, YELLOW, MEDIUM);

    // Draw dot showing the selected file
    if (num_items() > 1) {
        int span   = 100;  // degrees
        int dtheta = span * _selected / (num_items() - 1);
        int theta  = (span / 2) - dtheta;
        int dx, dy;
        r_degrees_to_xy(110, theta, &dx, &dy);

        drawFilledCircle({ dx, dy }, 8, WHITE);
    }
}

FileMenu wmbFileSelectScene;
#endif
