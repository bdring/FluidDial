// Copyright (c) 2023 - Barton Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Scene.h"
#include "FileParser.h"
#include "polar.h"

// #define SMOOTH_SCROLL
#define WRAP_FILE_LIST

extern Scene filePreviewScene;

extern Scene& jogScene;

class FileSelectScene : public Scene {
private:
    int              _selected_file = 0;
    std::vector<int> prevSelect;
    std::string      dirName         = "/sd";
    int              dirLevel        = 0;
    bool             _selecting_file = false;

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
    }

    void onDialButtonPress() { pop_scene(); }

    void onGreenButtonPress() {
        if (state != Idle) {
            return;
        }
        if (fileVector.size()) {
            fileInfo                                 = fileVector[_selected_file];
            prevSelect[(int)(prevSelect.size() - 1)] = _selected_file;
            if (fileInfo.isDir()) {
                prevSelect.push_back(0);
                dirName += "/";
                dirName += fileInfo.fileName;
                ++dirLevel;
                request_file_list(dirName.c_str());
            } else {
                std::string path(dirName);
                path += "/";
                path += fileInfo.fileName;
                push_scene(&filePreviewScene, (void*)path.c_str());
            }
        }
        ackBeep();
    }

    void onRedButtonPress() {
        if (state != Idle) {
            return;
        }
        if (dirLevel) {
            prevSelect.pop_back();
            auto pos = dirName.rfind('/');
            dirName  = dirName.substr(0, pos);
            --dirLevel;
            request_file_list(dirName.c_str());
        } else {
            prevSelect.clear();
            prevSelect.push_back(0);
            init_file_list();
        }
        ackBeep();
    }

    void onTouchPress() { _selecting_file = true; }
    void onTouchRelease() {
        if (_selecting_file) {
            _selecting_file = false;
            onGreenButtonPress();
        }
    }
    void onFilesList() override {
        _selected_file = prevSelect.back();
        reDisplay();
    }

    void onEncoder(int delta) override { scroll(delta); }

    void onMessage(char* command, char* arguments) override {
        dbg_printf("FileSelectScene::onMessage(\"%s\", \"%s\")\r\n", command, arguments);
        // now just need to know what to do with messages
    }

    void buttonLegends() {
        const char* grnLabel = "";
        const char* redLabel = "";

        if (state == Idle) {
            redLabel = dirLevel ? "Up.." : "Refresh";
            if (fileVector.size()) {
                grnLabel = fileVector[_selected_file].isDir() ? "Down.." : "Load";
            }
        }

        drawButtonLegends(redLabel, grnLabel, "Back");
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

    void onRightFlick() { activate_scene(&jogScene); }

    void showFiles(int yo) {
        // canvas.createSprite(240, 240);
        // drawBackground(BLACK);
        background();
        drawMenuTitle(current_scene->name());
        std::string fName;
        int         finfoT_color = BLUE;

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
                std::string fInfoT = "";  // file info top line
                std::string fInfoB = "";  // File info bottom line
                int         ext    = fName.rfind('.');
                if (fileVector.size()) {
                    if (fileVector[_selected_file].isDir()) {
                        fInfoB     = "Folder";
                        middle_txt = BLUE;
                    } else {
                        if (ext > 0) {
                            fInfoT = fName.substr(ext, fName.length());
                            fInfoT += " file";
                            fName.erase(ext);
                        }
                        fInfoB = format_size(fileVector[_selected_file].fileSize);
                    }
                }

                // progressbar
                if (yo == 0 && (fileVector.size() > 3)) {  // three or less are all displayed
                    for (int i = 0; i < 6; i++) {
                        canvas.drawArc(120, 120, 118 - i, 115 - i, -50, 50, DARKGREY);
                    }

                    int x, y;
                    int arc_degrees = 100;
                    int divisor     = fileVector.size() - 1;
                    int increment   = arc_degrees / divisor;
                    int start_angle = (arc_degrees / 2);
                    int angle       = start_angle - (_selected_file * arc_degrees) / divisor;
                    r_degrees_to_xy(114, angle, &x, &y);
                    canvas.fillCircle(120 + x, 120 - y, 5, LIGHTGREY);
                }

                if (yo == 0) {
                    auto top    = box[fi - 1];
                    auto bottom = box[fi + 1];
                    canvas.fillRoundRect(middle._xb, yo + middle._yb, middle._w, middle._h, middle._h / 2, middle._bg);
                    text(fInfoT.c_str(), top._xt, yo + top._yt, finfoT_color, top._f, top_center);
                    text(fInfoB.c_str(), bottom._xt, yo + bottom._yt, bottom._txt, bottom._f, top_center);
                }
            }  // if (fx == 1)

            if ((yo == 0) || (yo < 0 && yo + middle._yt > 45) || (yo > 0 && yo + middle._yt + middle._h < 202)) {
                auto_text(fName, middle._xt, yo + middle._yt, middle._w, (yo) ? WHITE : middle_txt, middle._f, middle_center);
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
        drawStatusSmall(21);
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
            delay_ms(10);
            yo -= yinc;
        }
#endif
        _selected_file = nextSelect;
        showFiles(0);
    }

    void reDisplay() {
        showFiles(0);
    }
};
FileSelectScene fileSelectScene;
