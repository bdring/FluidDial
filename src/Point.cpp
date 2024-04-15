// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Point.h"
#include "System.h"

Point Point::to_display() const {
    return { display.width() / 2 + x, display.height() / 2 - y };
}
Point Point::from_display() const {
    return { x - display.width() / 2, display.height() / 2 - y };
}
