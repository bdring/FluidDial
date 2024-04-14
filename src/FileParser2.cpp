#if 0
// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#    include "FileParser2.h"

#    include "FileMenu.h"
#    include "GrblParserC.h"  // send_line()

#    include <JsonStreamingParser.h>
#    include <JsonListener.h>

extern FileMenu wmbFileSelectScene;

JsonStreamingParser parser;

// This is necessary because of an annoying "feature" of JsonStreamingParser.
// After it issues an endDocument, it sets its internal state to STATE_DONE,
// in which it ignores everything.  You cannot reset the parser in the endDocument
// handler because it sets that state afterwards.  So we have to record the fact
// that an endDocument has happened and do the reset later, when new data comes in.
bool parser_needs_reset = true;

int         dirLevel = 0;
std::string dirName("/sd");

std::string current_filename;

void enter_directory(const char* name) {
    dirName += "/";
    dirName += name;

    ++dirLevel;
    request_file_list();
}
void exit_directory() {
    if (dirLevel) {
        auto pos = dirName.rfind('/');
        dirName  = dirName.substr(0, pos);
        --dirLevel;
        request_file_list();
    }
}

static bool fileinfoCompare(Item& f1, Item& f2) {
    // sort into filename order, with files first and folders second (same as on webUI)
    return f1.name().compare(f2.name()) < 0;
}

std::vector<std::string> fileLines;

class FilesListListener : public JsonListener {
private:
    bool        keyIsName;
    bool        keyIsSize;
    std::string fileName;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override { wmbFileSelectScene.removeAllItems(); }
    void startObject() override {
        keyIsName = keyIsSize = false;
        fileName              = "";
    }

    void key(const char* key) override {
        if (strcmp(key, "name") == 0) {
            keyIsName = true;  // gets reset in endObject()
            return;
        }
        if (strcmp(key, "size") == 0) {
            keyIsSize = true;  // gets reset in endObject()
            return;
        }
    }

    void value(const char* value) override {
        if (keyIsSize) {
            int size = atoi(value);
            dbg_printf("size %d\n", size);
            if (size < 0) {
                fileName += "/";
            }
            keyIsSize = false;
            return;
        }
        if (keyIsName) {
            fileName  = value;
            keyIsName = false;
            return;
        }
    }

    void endArray() override {
        // std::sort(fileVector.begin(), fileVector.end(), fileinfoCompare);
    }

    void endObject() override {
        if (fileName.length()) {
            wmbFileSelectScene.addItem(new FileItem(fileName.c_str()));
        }
    }

    //#define DEBUG_FILE_LIST
    void endDocument() override {
        init_listener();
        current_scene->onFilesList();
    }
} filesListListener;
#endif
