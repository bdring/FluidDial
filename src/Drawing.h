// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Factors for drawing parts of the pendant display

#pragma once
#include "FluidNCModel.h"
#include "Text.h"

class Stripe {
private:
    int       _x;
    int       _width;
    int       _height;
    fontnum_t _font;
    const int _text_inset = 5;

protected:
    Area* _area;
    int   _y;

    int text_left_x() { return _x + _text_inset; }
    int text_center_x() { return _x + _width / 2; }
    int text_right_x() { return _x + _width - _text_inset; }
    int text_middle_y() { return _y + _height / 2 + 2; }
    int widget_left_x() { return _x; }

public:
    Stripe(Area* area, int x, int y, int width, int height, fontnum_t font);
    void draw(const char* left, const char* right, bool highlighted, int left_color = WHITE);
    void draw(char left, const char* right, bool highlighted, int left_color = WHITE);
    void draw(const char* center, bool highlighted);
    int  y() { return _y; }
    int  gap() { return _height + 1; }
    void advance() { _y += gap(); }
};
class LED {
private:
    Area* _area;

    int _x;
    int _y;
    int _radius;
    int _gap;

public:
    LED(Area* area, int x, int y, int radius, int gap) : _area(area), _x(x), _y(y), _radius(radius), _gap(gap) {}
    void draw(bool highlighted);
};

class DRO : public Stripe {
private:
    void putDigit(int& n, int x, int y, int color);
    void fancyNumber(pos_t n, int n_decimals, int hl_digit, int x, int y, int text_color, int hl_text_color);

public:
    DRO(Area* area, int x, int y, int width, int height) : Stripe(area, x, y, width, height, MEDIUM_MONO) {}
    void draw(int axis, bool highlight);
    void draw(int axis, int hl_digit, bool highlight);
    void drawHoming(int axis, bool highlight, bool homed);
};
