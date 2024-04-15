// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Menu.h"

class FileItem : public Item {
private:
public:
    FileItem(const char* name) : Item(name) {}
    void invoke(void* arg) override {
        // doFileScreen(_name);
    }
    void show(const Point& where) override;
};

class FileMenu : public Menu {
private:
    int         _selected_file = 0;
    std::string _dirname       = "/";

public:
    FileMenu() : Menu("Files") {}

    const std::string& selected_name();
    void               onEntry(void* arg) override;

    void onRedButtonPress() override;
    void onFilesList() override;

    void onDialButtonPress() override;

    void onGreenButtonPress() override;
    void reDisplay() override;

    void buttonLegends();
    void rotate(int delta) override;
    int  touchedItem(int x, int y) override;

    void menuBackground() override;
};
