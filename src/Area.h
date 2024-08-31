#pragma once
#include "Point.h"

#ifdef USE_LOVYANGFX
#    include "LovyanGFX.h"
#else
#    include "M5GFX.h"
#endif

enum fontnum_t {
    TINY        = 0,
    SMALL       = 1,
    MEDIUM      = 2,
    LARGE       = 3,
    MEDIUM_MONO = 4,
};

class Area {
private:
    int          _x;
    int          _y;
    int          _w;
    int          _h;
    LGFX_Sprite* _sprite;

public:
    Area(LovyanGFX* parent, int depth, int x, int y, int w, int h) : _x(x), _y(y), _w(w), _h(h) {
        if (parent) {
            _sprite = new LGFX_Sprite(parent);
            _sprite->setColorDepth(depth);
            _sprite->createSprite(_w, _h);
        }
    }
    void set_xy(Point xy) {
        _x = xy.x;
        _y = xy.y;
    }
    LGFX_Sprite* sprite() { return _sprite; }

    int x() { return _x; }
    int y() { return _y; }
    int w() { return _w; }
    int h() { return _h; }

    Point to_display(Point);
    Point centered(Point);
    Point from_display(Point);

    bool is_inside(const Point screenxy, Point& localxy);

    // draw stuff
    // Routines that take Point as an argument work in a coordinate
    // space where 0,0 is at the center of the display and +Y is up

    void drawBackground(int color);

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

    void drawPngFile(const char* filename, int x, int y);
    void drawPngFile(const char* filename, Point xy);
    void drawPngBackground(const char* filename);

    void drawArc(Point xy, int r0, int r1, float angle0, float angle1, int color);

    // adjusts text to fit in (w) display area. reduces font size until it. tryfonts::false just uses fontnum
    void auto_text(const std::string& txt,
                   int                x,
                   int                y,
                   int                w,
                   int                color,
                   fontnum_t          fontnum  = MEDIUM,
                   int                datum    = middle_center,
                   bool               tryfonts = true,
                   bool               trimleft = false);

    void auto_text(const std::string& txt,
                   Point              xy,
                   int                w,
                   int                color,
                   fontnum_t          fontnum  = MEDIUM,
                   int                datum    = middle_center,
                   bool               tryfonts = true,
                   bool               trimleft = false);

    void text(const char* msg, int x, int y, int color, fontnum_t fontnum = TINY, int datum = middle_center);
    void text(const std::string& msg, int x, int y, int color, fontnum_t fontnum = TINY, int datum = middle_center);

    void text(const char* msg, Point xy, int color, fontnum_t fontnum = TINY, int datum = middle_center);
    void text(const std::string& msg, Point xy, int color, fontnum_t fontnum = TINY, int datum = middle_center);

    void centered_text(const char* msg, int y, int color = WHITE, fontnum_t fontnum = TINY);
    void centered_text(const std::string& msg, Point xy, int color = WHITE, fontnum_t fontnum = TINY);

    void refreshDisplay();
};

#if 0
extern Area* theArea;
LGFX_Sprite* area();

void set_area(Area* newArea);
void restore_area();
#endif
