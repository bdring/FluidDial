// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "System.h"
#include "Menu.h"

class VMenu {
private:
    int                _item_height;
    std::vector<Item*> _items;

public:
    Area* _area;
    VMenu(Area* area) : _area(area) {}

    void reDisplay();
    void addItem(Item* item);
    bool is_touched(int x, int y);
};
