#include "System.h"

#if 0
Area* theArea;

static Area* saved_area;

void set_area(Area* newArea) {
    saved_area = theArea;
    theArea    = newArea;
}
void restore_area() {
    theArea = saved_area;
}
#endif

Point Area::to_display(Point p) {
    return { _x + p.x, _y + p.y };
}

Point Area::centered(Point p) {
    return { _w / 2 + p.x, _h / 2 - p.y };
}

Point Area::from_display(Point p) {
    return { p.x - _w / 2 - _x, _h / 2 - p.y - _y };
}

bool Area::is_inside(const Point screenxy, Point& localxy) {
    if (screenxy.x < _x || screenxy.y < _y) {
        return false;
    }
    int offsetx = screenxy.x - _x;
    int offsety = screenxy.y - _y;
    if (offsetx >= _w || offsety >= _h) {
        return false;
    }
    localxy.x = offsetx;
    localxy.y = offsety;
    return true;
}

void Area::drawBackground(int color) {
    _sprite->fillSprite(color);
}

void Area::drawFilledCircle(int x, int y, int radius, int fillcolor) {
    _sprite->fillCircle(x, y, radius, fillcolor);
}
void Area::drawFilledCircle(Point xy, int radius, int fillcolor) {
    Point ctrxy = centered(xy);
    drawFilledCircle(ctrxy.x, ctrxy.y, radius, fillcolor);
}

void Area::drawCircle(int x, int y, int radius, int thickness, int outlinecolor) {
    for (int i = 0; i < thickness; i++) {
        _sprite->drawCircle(x, y, radius - i, outlinecolor);
    }
}
void Area::drawCircle(Point xy, int radius, int thickness, int outlinecolor) {
    Point ctrxy = centered(xy);
    drawCircle(ctrxy.x, ctrxy.y, radius, thickness, outlinecolor);
}

void Area::drawOutlinedCircle(int x, int y, int radius, int fillcolor, int outlinecolor) {
    _sprite->fillCircle(x, y, radius, fillcolor);
    _sprite->drawCircle(x, y, radius, outlinecolor);
}
void Area::drawOutlinedCircle(Point xy, int radius, int fillcolor, int outlinecolor) {
    Point ctrxy = centered(xy);
    drawOutlinedCircle(ctrxy.x, ctrxy.y, radius, fillcolor, outlinecolor);
}

void Area::drawRect(int x, int y, int width, int height, int radius, int bgcolor) {
    _sprite->fillRoundRect(x, y, width, height, radius, bgcolor);
}
void Area::drawRect(Point xy, int width, int height, int radius, int bgcolor) {
    Point offsetxy = { width / 2, -height / 2 };  // { 30, -30}
    Point ctrxy    = centered((xy - offsetxy));   // {i
    drawRect(ctrxy.x, ctrxy.y, width, height, radius, bgcolor);
}
void Area::drawRect(Point xy, Point wh, int radius, int bgcolor) {
    drawRect(xy, wh.x, wh.y, radius, bgcolor);
}

void Area::drawOutlinedRect(int x, int y, int width, int height, int bgcolor, int outlinecolor) {
    _sprite->fillRoundRect(x, y, width, height, 5, bgcolor);
    _sprite->drawRoundRect(x, y, width, height, 5, outlinecolor);
}
void Area::drawOutlinedRect(Point xy, int width, int height, int bgcolor, int outlinecolor) {
    Point ctrxy = centered(xy);
    drawOutlinedRect(ctrxy.x, ctrxy.y, width, height, bgcolor, outlinecolor);
}
// Area::drawPngFile(name, x, y) is defined in System*.cpp
void Area::drawPngFile(const char* name, Point xy) {
    Point ctrxy = centered(xy);
    drawPngFile(name, xy.x, xy.y);
}
void Area::drawPngBackground(const char* filename) {
    drawPngFile(filename, 0, 0);
}

void Area::drawArc(Point center, int r0, int r1, float angle0, float angle1, int color) {
    Point ctr = centered(center);
    _sprite->drawArc(ctr.x, ctr.y, r0, r1, angle0, angle1, color);
}

static const GFXfont* font[] = {
    // lgfx::v1::IFont* font[] = {
    &fonts::FreeSansBold9pt7b,   // TINY
    &fonts::FreeSansBold12pt7b,  // SMALL
    &fonts::FreeSansBold18pt7b,  // MEDIUM
    &fonts::FreeSansBold24pt7b,  // LARGE
    &fonts::FreeMonoBold18pt7b,  // MEDIUM_MONO
};

void Area::text(const char* msg, int x, int y, int color, fontnum_t fontnum, int datum) {
    _sprite->setFont(font[fontnum]);
    _sprite->setTextDatum(datum);
    _sprite->setTextColor(color);
    _sprite->drawString(msg, x, y);
}
void Area::text(const std::string& msg, int x, int y, int color, fontnum_t fontnum, int datum) {
    text(msg.c_str(), x, y, color, fontnum, datum);
}

void Area::text(const char* msg, Point xy, int color, fontnum_t fontnum, int datum) {
    Point ctrxy = centered(xy);
    text(msg, ctrxy.x, ctrxy.y, color, fontnum, datum);
}
void Area::text(const std::string& msg, Point xy, int color, fontnum_t fontnum, int datum) {
    text(msg.c_str(), xy, color, fontnum, datum);
}

void Area::centered_text(const char* msg, int y, int color, fontnum_t fontnum) {
    //    text(msg, display_short_side() / 2, y, color, fontnum);
    text(msg, _sprite->width() / 2, y, color, fontnum);
}

void Area::centered_text(const std::string& msg, Point xy, int color, fontnum_t fontnum) {
    Point ctrxy = centered(xy);
    text(msg, ctrxy.x, ctrxy.y, color, fontnum);
}

void Area::auto_text(const std::string& txt, int x, int y, int w, int color, fontnum_t fontnum, int datum, bool tryfonts, bool trimleft) {
    bool doesnotfit = true;
    while (true) {  // forever loop
        int f = fontnum;
        if (_sprite->textWidth(txt.c_str(), font[fontnum]) <= w) {
            doesnotfit = false;
            break;
        }
        if (fontnum && tryfonts) {
            fontnum = (fontnum_t)(--f);
        } else {
            break;
        }
    }

    std::string s(txt);

    if (doesnotfit) {
        int dotswidth = _sprite->textWidth(" ...");

        while (s.length() > 4) {
            if (trimleft) {
                s.erase(0, 1);
            } else {
                s.erase(s.length() - 1);
            }
            if (_sprite->textWidth(s.c_str()) + dotswidth <= w) {
                if (trimleft) {
                    s.insert(0, "... ");
                } else {
                    s += " ...";
                }
                break;
            }
        }
    }

    text(s, x, y, color, fontnum, datum);
}
void Area::auto_text(const std::string& txt, Point xy, int w, int color, fontnum_t fontnum, int datum, bool tryfonts, bool trimleft) {
    Point ctrxy = centered(xy);
    auto_text(txt, ctrxy.x, ctrxy.y, w, color, fontnum, datum, tryfonts, trimleft);
}

void Area::refreshDisplay() {
    display.startWrite();
    _sprite->pushSprite(x(), y());
    display.endWrite();
}
