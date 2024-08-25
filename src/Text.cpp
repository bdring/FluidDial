// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Text.h"
#include <map>

const GFXfont* font[] = {
    // lgfx::v1::IFont* font[] = {
    &fonts::FreeSansBold9pt7b,   // TINY
    &fonts::FreeSansBold12pt7b,  // SMALL
    &fonts::FreeSansBold18pt7b,  // MEDIUM
    &fonts::FreeSansBold24pt7b,  // LARGE
    &fonts::FreeMonoBold18pt7b,  // MEDIUM_MONO
};

void text(const char* msg, int x, int y, int color, fontnum_t fontnum, int datum) {
    canvas.setFont(font[fontnum]);
    canvas.setTextDatum(datum);
    canvas.setTextColor(color);
    canvas.drawString(msg, x, y);
}
void text(const std::string& msg, int x, int y, int color, fontnum_t fontnum, int datum) {
    text(msg.c_str(), x, y, color, fontnum, datum);
}

void text(const char* msg, Point xy, int color, fontnum_t fontnum, int datum) {
    Point dispxy = xy.to_display();
    text(msg, dispxy.x, dispxy.y, color, fontnum, datum);
}
void text(const std::string& msg, Point xy, int color, fontnum_t fontnum, int datum) {
    text(msg.c_str(), xy, color, fontnum, datum);
}

void centered_text(const char* msg, int y, int color, fontnum_t fontnum) {
    //    text(msg, display_short_side() / 2, y, color, fontnum);
    text(msg, canvas.width() / 2, y, color, fontnum);
}

void auto_text(const std::string& txt, int x, int y, int w, int color, fontnum_t fontnum, int datum, bool tryfonts, bool trimleft) {
    bool doesnotfit = true;
    while (true) {  // forever loop
        int f = fontnum;
        if (canvas.textWidth(txt.c_str(), font[fontnum]) <= w) {
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
        int dotswidth = canvas.textWidth(" ...");

        while (s.length() > 4) {
            if (trimleft) {
                s.erase(0, 1);
            } else {
                s.erase(s.length() - 1);
            }
            if (canvas.textWidth(s.c_str()) + dotswidth <= w) {
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
void auto_text(const std::string& txt, Point xy, int w, int color, fontnum_t fontnum, int datum, bool tryfonts, bool trimleft) {
    Point dispxy = xy.to_display();
    auto_text(txt, dispxy.x, dispxy.y, w, color, fontnum, datum, tryfonts, trimleft);
}
