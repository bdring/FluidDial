// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PieMenu.h"
#include "System.h"
#include "Drawing.h"
#include "polar.h"

void PieMenu::calculatePositions() {
    _num_slopes = num_items() / 2;  // Rounded down

    _slopes.clear();
    int dtheta = 360 / num_items();

    int angle = 90 - (dtheta / 2);
    for (size_t i = 0; i < _num_slopes; i++) {
        int slope = r_degrees_to_slope(1024, angle);
        _slopes.push_back(slope);
        angle -= dtheta;
    }

    int layout_radius = display_short_side() / 2 - _item_radius - 3;
    angle             = 90;
    for (size_t i = 0; i < num_items(); i++) {
        int x, y;
        r_degrees_to_xy(layout_radius, angle, &x, &y);
        Point center { x, y };
        setPosition(i, center);
        angle -= dtheta;
    }
}

int PieMenu::touchedItem(int x, int y) {
    // Convert from screen coordinates to 0,0 in the center
    Point ctr = Point { x, y }.from_display();

    fnc_realtime(StatusReport);  // used to update if status is out of sync

    x = ctr.x;
    y = ctr.y;

    int dead_radius = display_short_side() / 2 - _item_radius * 2;

    if ((x * x + y * y) < (dead_radius * dead_radius)) {
        return -1;  // In middle dead zone
    }
    if (x == 0) {
        x = 1;  // Don't divide by zero
    }
    int slope = y * 1024 / x;
    if (x < 0) {
        slope = -slope;
    }
    if (slope > _slopes[0]) {
        // Top item
        return 0;
    }
    // Side items
    size_t i;
    for (i = 1; i < _num_slopes; ++i) {
        if (slope > _slopes[i]) {
            return x > 0 ? i : num_items() - i;
        }
    }
    // If num_items() is even, return the bottom item  (i == num_items()-i)
    // If it is odd, return one of two bottom items stradding -Y axis

    reDisplay();
    return x > 0 ? i : num_items() - i;
}
void PieMenu::menuBackground() {
    background();
    text(selectedItem()->name(), { 0, -15 }, WHITE, SMALL);
    drawStatusSmall(90);
}

void PieMenu::onTouchFlick() {
    int item = touchedItem(touchX, touchY);
    if (item != -1) {
        select(item);
        ackBeep();
        invoke();
    }
}
void PieMenu::onDialButtonPress() {
    invoke();
}

void PieMenu::onTouchHold() {
    int item = touchedItem(touchX, touchY);
    if (item != -1) {
        select(item);
    }
}

void PieMenu::onTouchClick() {
    if (_help_text && touchIsCenter()) {
        push_scene(&helpScene, (void*)_help_text);
        return;
    }

    int item = touchedItem(touchX, touchY);
    if (item != -1) {
        select(item);
        invoke();
    }
}

void PieMenu::onStateChange(state_t old_state) {
    reDisplay();
}
