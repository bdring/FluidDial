// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <Arduino.h>
#include "Scene.h"
#include "FileParser.h"

// #define SMOOTH_SCROLL
#define WRAP_FILE_LIST
// clang-format off
#if 0
#define DBG_WRAP_FILES(...) log_printf(__VA_ARGS__)
#else
#define DBG_WRAP_FILES(...)
#endif

#if 0
#define DBG_PREV_SELECT(...) log_printf(__VA_ARGS__)
#else
#define DBG_PREV_SELECT(...)
#endif
// clang-format on

extern Scene filePreviewScene;

String displayTitle = "Files";

class FileSelectScene : public Scene {
private:
    int              _selected_file = 0;
    std::vector<int> prevSelect;

    const char* format_size(size_t size) {
        const int   buflen = 30;
        static char buffer[buflen];
        if (size >= 1000000) {
            int mb      = size / 1000000;
            int residue = size % 1000000;
            snprintf(buffer, buflen, "%d.%03d MB", mb, residue / 1000);
        } else if (size > 1000) {
            int kb      = size / 1000;
            int residue = size % 1000;
            snprintf(buffer, buflen, "%d.%03d KB", kb, residue);
        } else {
            snprintf(buffer, buflen, "%d bytes", size);
        }
        return buffer;
    }

public:
    FileSelectScene() : Scene("Files", 4) {}

    void onEntry(void* arg) {
        // a first time only thing, because files are already loaded
        if (prevSelect.size() == 0) {
            prevSelect.push_back(0);
        }
        DBG_PREV_SELECT("prevSelect::init:  size:%d, select:%d\r\n", prevSelect.size(), (prevSelect.size()) ? prevSelect.back() : 0);
    }

    void onDialButtonPress() { pop_scene(); }

    // XXX this should probably be a touch release on the file display
    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (fileVector.size()) {
            String dName;
            fileInfo                                 = fileVector[_selected_file];
            prevSelect[(int)(prevSelect.size() - 1)] = _selected_file;
            switch (fileInfo.fileType) {
                case 1:  //directory
                    prevSelect.push_back(0);
                    DBG_PREV_SELECT(
                        "prevSelect::push: size:%d, select:%d\r\n", prevSelect.size(), (prevSelect.size()) ? prevSelect.back() : 0);
                    enter_directory(fileInfo.fileName);
                    break;
                case 2:  // file
                    push_scene(&filePreviewScene, (void*)fileInfo.fileName.c_str());
                    break;
            }
        }
        ackBeep();
    }

    // XXX maybe a touch on the top of the screen i.e. the dirname field
    void onRedButtonPress() {
        if (state != Idle) {
            return;
        }
        if (dirLevel) {
            prevSelect.pop_back();
            DBG_PREV_SELECT("prevSelect::pop:  size:%d, select:%d\r\n", prevSelect.size(), (prevSelect.size()) ? prevSelect.back() : 0);
            exit_directory();
        } else {
            prevSelect.clear();
            prevSelect.push_back(0);
            init_file_list();
        }
        ackBeep();
    }

    void onTouchRelease(int x, int y) { onGreenButtonPress(); }

    void onFilesList() override {
        DBG_PREV_SELECT("prevSelect::back:  size:%d, select:%d\r\n", prevSelect.size(), (prevSelect.size()) ? prevSelect.back() : 0);
        _selected_file = prevSelect.back();
        reDisplay();
    }

    void onEncoder(int delta) override { scroll(delta); }

    void onMessage(char* command, char* arguments) override {
        log_printf("FileSelectScene::onMessage(\"%s\", \"%s\")\r\n", command, arguments);
        // now just need to know what to do with messages
    }

    void buttonLegends() {
        String grnText, redText = "";

        if (state == Idle) {
            redText = dirLevel ? "Up..." : "Refresh";
            if (fileVector.size()) {
                switch (fileVector[_selected_file].fileType) {
                    case 0:
                        break;
                    case 1:
                        grnText = "Load";
                        break;
                    case 2:
                        grnText = "Select";
                        break;
                }
            }
        }

        drawButtonLegends(redText, grnText, "Back");
    }

    struct {
        int       _xb;
        int       _xt;
        int       _yb;  // y for box
        int       _yt;  // y for text center
        int       _w;
        int       _h;
        fontnum_t _f;    // text font
        int       _bg;   // bg color
        int       _txt;  // text color
    } box[6] = {
        //    _xb, _xt, _yb, _yt,  _w, _h, _f,      _bg,  _txt
        { 0, 119, 22, 31, 156, 18, TINY, BLACK, WHITE },         // [0] "SD Files", "localFS", "etc...";
        { 20, 120, 45, 59, 190, 23, SMALL, BLACK, WHITE },       // [1]file[0] fileName
        { 5, 120, 77, 82, 210, 18, SMALL, LIGHTGREY, BLUE },     // [2]file[1] Info Line Top
        { 0, 120, 78, 120, 225, 80, MEDIUM, LIGHTGREY, BLACK },  // [3]file[1] fileName
        { 5, 120, 136, 139, 210, 18, TINY, LIGHTGREY, BLACK },   // [4]file[1] Info Line Bottom
        { 20, 120, 168, 181, 200, 23, SMALL, BLACK, WHITE },     // [5]file[2] fileName
    };
    int box_fi[3] = { 1, 3, 5 };

    void showFiles(int yo) {
        canvas.createSprite(240, 240);
        drawBackground(BLACK);
        displayTitle = current_scene->name();
        drawStatusTiny(20);
        drawMenuTitle(displayTitle);
        String fName;
        int    finfoT_color = BLUE;

        int fdIter = _selected_file - 1;  // first file in display list

        for (int fx = 0; fx < 3; fx++, fdIter++) {
            int  fi     = box_fi[fx];
            auto middle = box[fi];

#ifdef WRAP_FILE_LIST
            if (fileVector.size() > 2) {
                if (fdIter < 0) {
                    // last file first in list
                    fdIter = fileVector.size() - 1;
                } else if (fdIter > fileVector.size() - 1) {
                    // first file last in list
                    fdIter = 0;
                }
            }
#endif
            if (fdIter < 0) {
                if (yo == 0) {
                    DBG_WRAP_FILES("showFiles(): fx:%2d, fdIter:%2d, _selected_file:%2d\r\n", fx, fdIter, _selected_file);
                }
                continue;
            }

            fName = "< no files >";
            if (fileVector.size()) {
                fName = fileVector[fdIter].fileName;
            }
            if (yo == 0 && middle._bg != BLACK) {
                canvas.fillRoundRect(middle._xb, yo + middle._yb, middle._w, middle._h, middle._h / 2, middle._bg);
            }
            int middle_txt = middle._txt;
            if (fx == 1) {
                String fInfoT = "";  // file info top line
                String fInfoB = "";  // File info bottom line
                int    ext    = fName.lastIndexOf('.');
                float  fs     = 0.0;
                if (fileVector.size()) {
                    switch (fileVector[_selected_file].fileType) {
                        case 0:
                            break;
                        case 1:
                            fInfoB     = "Folder";
                            middle_txt = BLUE;
                            break;
                        case 2:
                            if (ext > 0) {
                                fInfoT = fName.substring(ext, fName.length());
                                fInfoT += " file";
                                fName.remove(ext);
                            }
                            fInfoB = format_size(fileVector[_selected_file].fileSize);
                            break;
                    }
                }

                // progressbar
                if (yo == 0 && (fileVector.size() > 3)) {  // three or less are all displayed
                    for (int i = 0; i < 6; i++) {
                        canvas.drawArc(120, 120, 118 - i, 115 - i, -50, 50, DARKGREY);
                    }

                    float mx  = 1.745;
                    float s   = mx / -2.0;
                    float inc = mx / (float)(fileVector.size() - 1);

                    int x = cosf(s + inc * (float)_selected_file) * 114.0;
                    int y = sinf(s + inc * (float)_selected_file) * 114.0;
                    canvas.fillCircle(x + 120, y + 120, 5, LIGHTGREY);
                }

                if (yo == 0) {
                    auto top    = box[fi - 1];
                    auto bottom = box[fi + 1];
                    canvas.fillRoundRect(middle._xb, yo + middle._yb, middle._w, middle._h, middle._h / 2, middle._bg);
                    text(fInfoT, top._xt, yo + top._yt, finfoT_color, top._f, top_center);
                    text(fInfoB, bottom._xt, yo + bottom._yt, bottom._txt, bottom._f, top_center);
                }
            }  // if (fx == 1)

            if ((yo == 0) || (yo < 0 && yo + middle._yt > 45) || (yo > 0 && yo + middle._yt + middle._h < 202)) {
                auto_text(fName, middle._xt, yo + middle._yt, middle._w, (yo) ? WHITE : middle_txt, middle._f, middle_center);
            }
            if (yo == 0) {
                DBG_WRAP_FILES("showFiles(): fx:%2d, fdIter:%2d, _selected_file:%2d - %s\r\n", fx, fdIter, _selected_file, fName.c_str());
            }
            if (fx == 1) {
#ifdef WRAP_FILE_LIST
                if (fileVector.size() > 2) {
                    continue;
                }
#endif
                if (fdIter >= (int)(fileVector.size() - 1)) {
                    break;
                }
            }
        }  // for(fx)
        buttonLegends();
        refreshDisplay();
    }

    void scroll(int updown) {
        int nextSelect = _selected_file + updown;
#ifdef WRAP_FILE_LIST
        if (fileVector.size() < 3) {
            if (nextSelect < 0 || nextSelect > (int)(fileVector.size() - 1)) {
                return;
            }
        } else {
            if (nextSelect < 0) {
                nextSelect = fileVector.size() - 1;
            } else if (nextSelect > (int)(fileVector.size() - 1)) {
                nextSelect = 0;
            }
        }
#else
        if (nextSelect < 0 || nextSelect > (int)(fileVector.size() - 1)) {
            return;
        }
#endif

        const int yinc   = updown * 10;
        const int ylimit = 60;
        int       yo     = yinc;

#ifdef SMOOTH_SCROLL
        while (abs(yo) < ylimit) {
            showFiles(yo);
            delay(10);
            yo -= yinc;
        }
#endif
        _selected_file = nextSelect;
        showFiles(0);

        DBG_PREV_SELECT("scroll(%d): _selected_file:%2d, files:%2d\r\n", updown, _selected_file, fileVector.size());
    }

    void reDisplay() { showFiles(0); }
};
FileSelectScene filesScene;
