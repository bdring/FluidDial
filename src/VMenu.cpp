// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "VMenu.h"

void VMenu::addItem(Item* item) {
    _items.push_back(item);

    // Recalculate positions
    _item_height = _area->h() / _items.size();
    int center_y = (_area->h() - _item_height) / 2;

    for (auto const& item : _items) {
        item->set_position(Point { 0, center_y });
        center_y -= _item_height;
    }
}

void VMenu::reDisplay() {
    _area->drawBackground(TFT_BLACK);
    for (auto const& item : _items) {
        item->show(_area);
    }
    display.startWrite();
    _area->refreshDisplay();
}

bool VMenu::is_touched(int x, int y) {
    Point local;
    if (!_area->is_inside({ x, y }, local)) {
        return false;
    }
    int item_num = local.y / _item_height;

    _items[item_num]->invoke();
    return true;
}
