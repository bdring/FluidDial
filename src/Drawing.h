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
    int _y;

    int text_left_x() { return _x + _text_inset; }
    int text_center_x() { return _x + _width / 2; }
    int text_right_x() { return _x + _width - _text_inset; }
    int text_middle_y() { return _y + _height / 2 + 2; }
    int widget_left_x() { return _x; }

public:
    Stripe(int x, int y, int width, int height, fontnum_t font);
    void draw(const char* left, const char* right, bool highlighted, int left_color = WHITE);
    void draw(char left, const char* right, bool highlighted, int left_color = WHITE);
    void draw(const char* center, bool highlighted);
    int  y() { return _y; }
    int  gap() { return _height + 1; }
    void advance() { _y += gap(); }
};
class LED {
private:
    int _x;
    int _y;
    int _radius;
    int _gap;

public:
    LED(int x, int y, int radius, int gap) : _x(x), _y(y), _radius(radius), _gap(gap) {}
    void draw(bool highlighted);
};

class DRO : public Stripe {
public:
    DRO(int x, int y, int width, int height) : Stripe(x, y, width, height, MEDIUM_MONO) {}
    void draw(int axis, bool highlight);
    void draw(int axis, int hl_digit, bool highlight);
    void drawHoming(int axis, bool highlight, bool homed);
};

// draw stuff
// Routines that take Point as an argument work in a coordinate
// space where 0,0 is at the center of the display and +Y is up

LGFX_Sprite* createPngBackground(const char* filename);

void drawBackground(LGFX_Sprite* sprite);
void drawBackground(int color);
void drawStatus();
void drawStatusTiny(int y);
void drawStatusSmall(int y);

void drawFilledCircle(int x, int y, int radius, int fillcolor);
void drawFilledCircle(Point xy, int radius, int fillcolor);

void drawCircle(int x, int y, int radius, int thickness, int outlinecolor);
void drawCircle(Point xy, int radius, int thickness, int outlinecolor);

void drawOutlinedCircle(int x, int y, int radius, int fillcolor, int outlinecolor);
void drawOutlinedCircle(Point xy, int radius, int fillcolor, int outlinecolor);

void drawRect(int x, int y, int width, int height, int radius, int bgcolor);
void drawRect(Point xy, int width, int height, int radius, int bgcolor);
void drawRect(Point xy, Point wh, int radius, int bgcolor);

void drawOutlinedRect(int x, int y, int width, int height, int bgcolor, int outlinecolor);
void drawOutlinedRect(Point xy, int width, int height, int bgcolor, int outlinecolor);

void drawButtonLegends(const char* red, const char* green, const char* orange);
void drawMenuTitle(const char* name);

void drawPngFile(const char* filename, Point xy);
void drawPngBackground(const char* filename);

void refreshDisplay();

void drawError();

extern Point sprite_offset;
