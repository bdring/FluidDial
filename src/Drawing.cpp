// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "System.h"
#include "Drawing.h"
#include <map>

#include "Scene.h"

Stripe::Stripe(Area* area, int x, int y, int width, int height, fontnum_t font) :
    _area(area), _x(x), _y(y), _width(width), _height(height), _font(font) {}

void Stripe::draw(const char* left, const char* right, bool highlighted, int left_color) {
    _area->drawOutlinedRect(_x, _y, _width, _height, highlighted ? BLUE : NAVY, WHITE);
    if (*left) {
        _area->text(left, text_left_x(), text_middle_y(), left_color, _font, middle_left);
    }
    if (*right) {
        _area->text(right, text_right_x(), text_middle_y(), WHITE, _font, middle_right);
    }
    advance();
}
void Stripe::draw(char left, const char* right, bool highlighted, int left_color) {
    char t[2] = { left, '\0' };
    draw(t, right, highlighted, left_color);
}
void Stripe::draw(const char* center, bool highlighted) {
    _area->drawOutlinedRect(_x, _y, _width, _height, highlighted ? BLUE : NAVY, WHITE);
    _area->text(center, text_center_x(), text_middle_y(), WHITE, _font, middle_center);
    advance();
}

void DRO::putDigit(int& n, int x, int y, int color) {
    char txt[2] = { '\0', '\0' };
    txt[0]      = "0123456789"[n % 10];
    n /= 10;
    _area->text(txt, x, y, color, MEDIUM, middle_right);
}
void DRO::fancyNumber(pos_t n, int n_decimals, int hl_digit, int x, int y, int text_color, int hl_text_color) {
    fontnum_t font     = SMALL;
    int       n_digits = n_decimals + 1;
    int       i;
    bool      isneg = n < 0;
    if (isneg) {
        n = -n;
    }
#ifdef E4_POS_T
    // in e4 format, the number always has 4 postdecimal digits,
    // so if n_decimals is less than 4, we discard digits from
    // the right.  We could do this by computing a divisor
    // based on e4_power10(4 - n_decimals), but the expected
    // number of iterations of this loop is max 4, typically 2,
    // so that is hardly worthwhile.
    for (i = 4; i > n_decimals; --i) {
        if (i == (n_decimals + 1)) {  // Round
            n += 5;
        }
        n /= 10;
    }
#else
    for (i = 0; i < n_decimals; i++) {
        n *= 10;
    }
#endif
    const int char_width = 20;

    int ni = (int)n;
    for (i = 0; i < n_decimals; i++) {
        putDigit(ni, x, y, i == hl_digit ? hl_text_color : text_color);
        x -= char_width;
    }
    if (n_decimals) {
        _area->text(".", x - 10, y, text_color, MEDIUM, middle_center);
        x -= char_width;
    }
    do {
        putDigit(ni, x, y, i++ == hl_digit ? hl_text_color : text_color);
        x -= char_width;
    } while (ni || i <= hl_digit);
    if (isneg) {
        _area->text("-", x, y, text_color, MEDIUM, middle_right);
    }
}

void DRO::drawHoming(int axis, bool highlight, bool homed) {
    _area->text(axisNumToCStr(axis), text_left_x(), text_middle_y(), myLimitSwitches[axis] ? GREEN : YELLOW, MEDIUM, middle_left);
    fancyNumber(myAxes[axis], num_digits(), -1, text_right_x(), text_middle_y(), highlight ? (homed ? GREEN : RED) : DARKGREY, RED);
    advance();
}

void DRO::draw(int axis, int hl_digit, bool highlight) {
    _area->text(axisNumToCStr(axis), text_left_x(), text_middle_y(), highlight ? GREEN : DARKGREY, MEDIUM, middle_left);
    fancyNumber(
        myAxes[axis], num_digits(), hl_digit, text_right_x(), text_middle_y(), highlight ? WHITE : DARKGREY, highlight ? RED : DARKGREY);
    advance();
}

void DRO::draw(int axis, bool highlight) {
    Stripe::draw(axisNumToChar(axis), pos_to_cstr(myAxes[axis], num_digits()), highlight, myLimitSwitches[axis] ? GREEN : WHITE);
}

void LED::draw(bool highlighted) {
    _area->drawOutlinedCircle(_x, _y, _radius, (highlighted) ? GREEN : DARKGREY, WHITE);
    _y += _gap;
}
