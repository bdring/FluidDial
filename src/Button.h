#pragma once

#include "System.h"
#include "Drawing.h"
#include <functional>

struct Button {
    int         x, y, w, h;
    const char* label;
    int         fill_color;
    int         outline_color;
    int         text_color;
    std::function<void()> onPress;

    Button() : x(0), y(0), w(0), h(0), label(""), fill_color(NAVY),
               outline_color(WHITE), text_color(WHITE), onPress(nullptr) {}

    /**
     * Update button geometry, label, colors, and callback all at once, then draw.
     */
    void set(int nx, int ny, int nw, int nh, const char* nlabel, int nfill, int noutline, int ntext,
             std::function<void()> fn) {
        x = nx; y = ny; w = nw; h = nh;
        label = nlabel;
        fill_color = nfill;
        outline_color = noutline;
        text_color = ntext;
        onPress = fn;
        draw();  // draw immediately
    }

    /**
     * Draw the button.
     */
    void draw() const {
        drawOutlinedRect(x, y, w, h, fill_color, outline_color);
        centered_text(label, y + h / 2 + 3, text_color, SMALL);
    }

    /**
     * Check if touch hits this button and invoke callback.
     */
    bool handleTouch(int tx, int ty) const {
        if (tx >= x && tx <= x + w && ty >= y && ty <= y + h && onPress) {
            onPress();
            return true;
        }
        return false;
    }
};
