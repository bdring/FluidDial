// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Integer trig for converting between rectangular and polar coordinates,
// useful for calculating circular layouts on a graphics screen.

#include "polar.h"

#include <stdio.h>

// 2*pi << REVS_BITS
#define TWOPISCALED 25736

// Internal factor that converges quickly for angles less than 45 degrees
static void _r_revs_to_xy(int radius, int angle, int* px, int* py) {
    angle *= TWOPISCALED;
    angle >>= REVS_BITS;
    int adjust = radius * 4;
    int cos = 0, sin = 0;
    for (int i = 0; i < 10;) {
        cos += adjust;
        adjust *= angle;
        adjust /= ++i;
        adjust >>= REVS_BITS;
        if (adjust == 0) {
            break;
        }
        sin += adjust;
        adjust *= angle;
        adjust /= ++i;
        adjust >>= REVS_BITS;
        if (adjust == 0) {
            break;
        }
        adjust = -adjust;
    }
    *py = (sin + 2) / 4;
    *px = (cos + 2) / 4;
}

// External API.  Uses symmetries to reduce the angle to the range
// where _rtheta_to_xy() converges quickly.  The angle is scaled
// such that (1 << REVS_BITS) represents a full revolution.
void r_revs_to_xy(int radius, int angle, int* px, int* py) {
    if (angle < 0) {
        r_revs_to_xy(radius, -angle, px, py);
        *py = -(*py);
        return;
    }
    if (angle > (1 << (REVS_BITS - 1))) {  // > 180 degrees
        r_revs_to_xy(radius, (1 << (REVS_BITS)) - angle, px, py);
        *py = -*py;
        return;
    }
    if (angle > (1 << (REVS_BITS - 2))) {  // > 90 degrees
        r_revs_to_xy(radius, (1 << (REVS_BITS - 1)) - angle, px, py);
        *px = -(*px);
        return;
    }
    if (angle > (1 << (REVS_BITS - 3))) {
        r_revs_to_xy(radius, ((1 << (REVS_BITS - 2)) - angle), px, py);
        int sin = *px;
        *px     = *py;
        *py     = sin;
        return;
    }
    _r_revs_to_xy(radius, angle, px, py);
}

void r_degrees_to_xy(int radius, int degrees, int* px, int* py) {
    return r_revs_to_xy(radius, to_revs(degrees, 360), px, py);
}

// The result is scaled by the radius. E.g. if degrees is 45,
// for a slope of 1, the return value is radius.
int r_degrees_to_slope(int radius, int degrees) {
    int x, y;
    r_degrees_to_xy(radius, degrees, &x, &y);
    if (x == 0) {
        x = 1;
    }
    return y * radius / x;
}

// arctangent approximation per https://nghiaho.com/?p=997
// Radians:  z(M_PI_4 - (z - 1)*(0.2447 + 0.0663*z);
// Degrees:  z(45°−(z−1)(14°+3.83°z))
#define ATAN_SCALER 1024
#define ATAN_SCALE(n) (n * ATAN_SCALER)
#define ATAN_DOWNSCALE(n)                                                                                                                  \
    do {                                                                                                                                   \
        n += ATAN_SCALER / 2;                                                                                                              \
        n /= ATAN_SCALER;                                                                                                                  \
    } while (0)
#define ATAN_MUL(n, m)                                                                                                                     \
    do {                                                                                                                                   \
        n *= m;                                                                                                                            \
        ATAN_DOWNSCALE(n);                                                                                                                 \
    } while (0)

// Internal factor limited to 0 <= y/x <= 1
static int _iatan2_degrees(int x, int y) {
    if (x == 0) {
        return 90;
    }
    int z   = ATAN_SCALE(y) / x;
    int res = 3922 * z;  // 3922 = 3.83 * 1024
    ATAN_DOWNSCALE(res);
    res += ATAN_SCALE(14);
    ATAN_MUL(res, (z - ATAN_SCALE(1)));
    res = ATAN_SCALE(45) - res;

    ATAN_MUL(res, z);
    ATAN_DOWNSCALE(res);
    return res;
}

// XY to angle, result in degrees
int iatan2_degrees(int x, int y) {
    if (y < 0) {
        return -iatan2_degrees(x, -y);
    }
    if (x < 0) {
        return 180 - iatan2_degrees(-x, y);
    }
    if (y > x) {
        return 90 - iatan2_degrees(y, x);
    }
    return _iatan2_degrees(x, y);
}

// sqrt(x*x + y*y)
int imagnitude(int x, int y) {
    int n     = x * x + y * y;
    int res   = n;
    int trial = 1;
    while (res > trial) {
        res   = (res + trial) >> 1;
        trial = n / res;
    }
    return res;
}

void xy_to_r_degrees(int x, int y, int* radius, int* degrees) {
    *degrees = iatan2_degrees(x, y);
    *radius  = imagnitude(x, y);
}
