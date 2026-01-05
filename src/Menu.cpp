// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Menu.h"
#include "System.h"
#include "Drawing.h"

void do_nothing(void* foo) {}

void RoundButton::show(const Point& where) {
    drawOutlinedCircle(where, _radius, _highlighted ? _hl_fill_color : _fill_color, _highlighted ? _hl_outline_color : _outline_color);
    text(name().substr(0, 1), where, _highlighted ? MAROON : WHITE, MEDIUM);
}
// void ImageButton::show(const Point& where) {
//     if (_highlighted) {
//         drawFilledCircle(where, _radius + 3, _disabled ? DARKGREY : _outline_color);
//     } else {
//         drawFilledCircle(where, _radius - 2, _disabled ? DARKGREY : LIGHTGREY);
//     }
//     drawPngFile(_filename, where);
// }

// Optimized v1 (no alpha blending, simpler)
void ImageButton::show(const Point& where) {
    if (_highlighted) {
        drawFilledCircle(where, _radius + 2, _disabled ? DARKGREY : _outline_color);
        Point offsetPoint = where;
        offsetPoint.x-=1;
        offsetPoint.y+=1;
        drawFilledCircle(offsetPoint, _radius + 2, _disabled ? DARKGREY : _outline_color);
    }
    //drawFilledCircle(where, _radius - 1, BLACK);

    if(_img_cache == NULL)
    {
        _img_cache = new LGFX_Sprite(&canvas);
        _img_cache->setColorDepth(canvas.getColorDepth());
        _img_cache->createSprite(64,64);
        drawPngFile(_img_cache, _filename, 0,0);
    }
    Point tp = where.to_display();
    _img_cache->pushSprite(tp.x-32, tp.y-32, 0);
}

// v2, with alpha blending, at the cost of 3 sprite buffers, one for each state
// Considering we use a very low color depth (332 or 4 to 8 color per channel...)
// it maybe worth considering a redesign of the dial images to have sharp edges so
// that the v1 approach work and there would be no color innacuracy because of alpha blending
// This approach takes too much memory and causes issues
// void ImageButton::show(const Point& where) {
//     LGFX_Sprite *sprite;
 
//     // We can either do all image cache initially, or dynamically when needed,
//     // doing it initially offer the advantage of constant response time afterward
//     // at the expense of slightly longer initialization time
//     if(_img_cache_highlight == NULL){
//         _img_cache_highlight = new LGFX_Sprite(&canvas);
//         _img_cache_highlight->setColorDepth(canvas.getColorDepth());
//         _img_cache_highlight->createSprite(70,70);
//         _img_cache_highlight->fillCircle(35,35, _radius + 3, _outline_color);
//         drawPngFile(_img_cache_highlight, _filename, 0,0);                
//     }

//     if(_img_cache_disabled == NULL && _highlighted && _disabled){
//         _img_cache_disabled = new LGFX_Sprite(&canvas);
//         _img_cache_disabled->setColorDepth(canvas.getColorDepth());
//         _img_cache_disabled->createSprite(70,70);
//         _img_cache_disabled->fillCircle(35,35, _radius + 3, DARKGREY);
//         drawPngFile(_img_cache_disabled, _filename, 0,0);                
//     }

//     if(_img_cache == NULL)
//     {
//         _img_cache = new LGFX_Sprite(&canvas);
//         _img_cache->setColorDepth(canvas.getColorDepth());
//         _img_cache->createSprite(70,70);
//         drawPngFile(_img_cache, _filename, 0,0);
//     }

//     sprite = _highlighted ? (_disabled ? _img_cache_disabled : _img_cache_highlight) : _img_cache;
//     Point tp = where.to_display();
//     sprite->pushSprite(tp.x-35, tp.y-35, 0);
//     if(_img_cache_disabled != NULL)
//     {
//         delete _img_cache_disabled;
//         _img_cache_disabled = NULL;
//     }

// }


void RectangularButton::show(const Point& where) {
    drawOutlinedRect(where, _width, _height, _highlighted ? BLUE : _outline_color, _bg_color);
    text(_text, where, _text_color, SMALL);
}

void Menu::removeAllItems() {
    for (auto const& item : _items) {
        delete item;
    }
    _items.clear();
    _positions.clear();
    _num_items = 0;
}

void Menu::reDisplay() {
    menuBackground();
    show_items();
    refreshDisplay();
}
void Menu::rotate(int delta) {
    if (_selected != -1) {
        _items[_selected]->unhighlight();
    }

    int previous = _selected;
    do {
        _selected += delta;
        while (_selected < 0) {
            _selected += _num_items;
        }
        while (_selected >= _num_items) {
            _selected -= _num_items;
        }
        if (!_items[_selected]->hidden()) {
            break;
        }
        // If we land on a hidden item, move to the next item in the
        // same direction.
        delta = delta < 0 ? -1 : 1;
    } while (_selected != previous);

    _items[_selected]->highlight();
    reDisplay();
}
