// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Menu.h"

class MacroItem : public Item {
private:
    std::string _filename;

public:
    MacroItem(const char* name, std::string filename) : Item(name), _filename(filename) {}
    // "cmd:" macros hold a command line rather than a file to load
    bool is_command() const { return _filename.rfind("cmd:", 0) == 0; }
    void invoke(void* arg) override;
    void show(const Point& where) override;
};
