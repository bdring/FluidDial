// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Point.h"
#include "System.h"

Point Point::to_display() const {
    int center = display_short_side() / 2;
    return { center + x, center - y };
}
Point Point::from_display() const {
    int center = display_short_side() / 2;
    return { x - center, center - y };
}
