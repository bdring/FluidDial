// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Point.h"
#include "System.h"

#if 0
Point Point::to_display() const {
    return { scene_area->w() / 2 + x, scene_area->h() / 2 - y };
}
Point Point::from_display() const {
    return { x - scene_area->w() / 2, scene_area->h() / 2 - y };
}
#endif
