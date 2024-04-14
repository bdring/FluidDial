// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileParser.h"

#include "Scene.h"        // current_scene->reDisplay()
#include "GrblParserC.h"  // send_line()

#include <JsonStreamingParser.h>
#include <JsonListener.h>

fileinfo              fileInfo;
std::vector<fileinfo> fileVector;

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

static bool fileinfoCompare(const fileinfo& f1, const fileinfo& f2) {
    // sort into filename order, with files first and folders second (same as on webUI)
    if (f1.fileType == ORDINARY && f2.fileType == DIRECTORY) {
        return true;
    }
    if (f1.fileType == DIRECTORY && f2.fileType == ORDINARY) {
        return false;
    }
    if (f1.fileName.compare(f2.fileName) < 0) {
        return true;
    }
    return false;
}

std::vector<std::string> fileLines;

class FilesListListener : public JsonListener {
private:
    bool        haveNewFile;
    std::string current_key;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override {
        fileVector.clear();
        haveNewFile = false;
    }
    void startObject() override {}

    void key(const char* key) override {
        current_key = key;
        if (strcmp(key, "name") == 0) {
            haveNewFile = true;  // gets reset in endObject()
        }
    }

    void value(const char* value) override {
        if (current_key == "name") {
            fileInfo.fileName = value;
            return;
        }
        if (current_key == "size") {
            fileInfo.fileSize = atoi(value);
            fileInfo.fileType = fileInfo.fileSize < 0 ? DIRECTORY : ORDINARY;
        }
    }

    void endArray() override { std::sort(fileVector.begin(), fileVector.end(), fileinfoCompare); }

    void endObject() override {
        if (haveNewFile) {
            fileVector.push_back(fileInfo);
            haveNewFile = false;
        }
    }

    //#define DEBUG_FILE_LIST
    void endDocument() override {
#ifdef DEBUG_FILE_LIST
        int ix = 0;
        for (auto const& vi : fileVector) {
            USBSerial.printf("[%d] type: %d:%s:\"%s\", size: %d\r\n",
                             ix++,
                             vi.fileType,
                             (vi.fileType == 2) ? "file" : "dir ",
                             vi.fileName.c_str(),
                             vi.fileSize);
        }
#endif
        init_listener();
        current_scene->onFilesList();
    }
} filesListListener;

class FileLinesListener : public JsonListener {
private:
    bool _in_array;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override {
        fileLines.clear();
        _in_array = true;
    }
    void startObject() override {}

    void key(const char* key) override {
        //      if (strcmp(key, "path") == 0) {}
    }

    void value(const char* value) override {
        if (_in_array) {
            fileLines.push_back(value);
        }
    }

    void endArray() override { _in_array = false; }
    void endObject() override {}
    void endDocument() override {
        init_listener();
        current_scene->onFileLines();
    }
} fileLinesListener;

class InitialListener : public JsonListener {
private:
public:
    void whitespace(char c) override {}
    void startDocument() override {}
    void value(const char* value) override {}
    void endArray() override {}
    void endObject() override {}
    void endDocument() override {}
    void startArray() override {}
    void startObject() override {}

    void key(const char* key) override {
        if (strcmp(key, "files") == 0) {
            parser.setListener(&filesListListener);
            return;
        }
        if (strcmp(key, "file_lines") == 0) {
            parser.setListener(&fileLinesListener);
            return;
        }
    }
} initialListener;

void init_listener() {
    parser.setListener(&initialListener);
    parser_needs_reset = true;
}

void request_file_list() {
    send_linef("$Files/ListGCode=%s", dirName.c_str());
    parser_needs_reset = true;
}

void init_file_list() {
    init_listener();
    dirLevel = 0;
    dirName  = "/sd";
    request_file_list();
}

void request_file_preview(const char* name) {
    send_linef("$File/ShowSome=7,%s/%s", dirName.c_str(), name);
}
extern "C" void handle_msg(char* command, char* arguments) {
    if (strcmp(command, "RST") == 0) {
        dbg_println("FluidNC Reset");
    }
    if (strcmp(command, "Files changed") == 0) {
        dbg_println("Files changed");
        init_file_list();
    }
    if (strcmp(command, "JSON") == 0) {
        if (parser_needs_reset) {
            parser.reset();
            parser_needs_reset = false;
        }
        while (*arguments) {
            parser.parse(*arguments++);
        }
#define Ack 0xB2
        fnc_realtime((realtime_cmd_t)Ack);
    }
}
