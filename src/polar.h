// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Integer trig for converting between rectangular and polar coordinates,
// useful for calculating circular layouts on a graphics screen.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define REVS_BITS 12

// 1 << REVS_BITS is a full circle
// For, say, 45 degrees, use theta(45, 360);
inline int to_revs(int num, int denom) {
    return (num << REVS_BITS) / denom;
}
void r_revs_to_xy(int radius, int angle, int* px, int* py);
void r_degrees_to_xy(int radius, int degrees, int* px, int* py);
int  r_degrees_to_slope(int radius, int degrees);
int  iatan2(int x, int y);
int  imagnitude(int x, int y);
void xy_to_r_degrees(int x, int y, int* radius, int* theta);

#ifdef __cplusplus
}
#endif
