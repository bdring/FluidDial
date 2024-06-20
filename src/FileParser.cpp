// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileParser.h"

#include "Scene.h"  // current_scene->reDisplay()
#include "Menu.h"
#include "GrblParserC.h"  // send_line()
#include "HomingScene.h"  // set_axis_homed()

#include <JsonStreamingParser.h>
#include <JsonListener.h>

#include "MacroItem.h"

extern Menu macroMenu;

fileinfo              fileInfo;
std::vector<fileinfo> fileVector;

JsonStreamingParser parser;

// This is necessary because of an annoying "feature" of JsonStreamingParser.
// After it issues an endDocument, it sets its internal state to STATE_DONE,
// in which it ignores everything.  You cannot reset the parser in the endDocument
// handler because it sets that state afterwards.  So we have to record the fact
// that an endDocument has happened and do the reset later, when new data comes in.
bool parser_needs_reset = true;

static bool fileinfoCompare(const fileinfo& f1, const fileinfo& f2) {
    // sort into filename order, with files first and folders second (same as on webUI)
    if (!f1.isDir() && f2.isDir()) {
        return true;
    }
    if (f1.isDir() && !f2.isDir()) {
        return false;
    }
    if (f1.fileName.compare(f2.fileName) < 0) {
        return true;
    }
    return false;
}

int fileFirstLine = 0;

std::vector<std::string> fileLines;

extern JsonListener* pInitialListener;

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
            //            fileInfo.isDir    = fileInfo.fileSize < 0;
        }
    }

    void endArray() override {
        std::sort(fileVector.begin(), fileVector.end(), fileinfoCompare);
        current_scene->onFilesList();
        parser.setListener(pInitialListener);
    }

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
            dbg_printf("[%d] type: %s:\"%s\", size: %d\r\n", ix++, (vi.isDir()) ? "file" : "dir ", vi.fileName.c_str(), vi.fileSize);
        }
#endif
        init_listener();
    }
} filesListListener;

std::vector<Macro*> macros;

class MacroListListener : public JsonListener {
private:
    std::string* _valuep;

    std::string _name;
    std::string _filename;
    std::string _target;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override { macroMenu.removeAllItems(); }
    void startObject() override {
        _name.clear();
        _target.clear();
        _filename.clear();
    }

    void key(const char* key) override {
        if (strcmp(key, "name") == 0) {
            _valuep = &_name;
            return;
        }
        if (strcmp(key, "filename") == 0) {
            _valuep = &_filename;
            return;
        }
        if (strcmp(key, "target") == 0) {
            _valuep = &_target;
            return;
        }
        _valuep = nullptr;
    }

    void value(const char* value) override {
        if (_valuep) {
            *_valuep = value;
        }
    }

    void endArray() override {}
    void endObject() override {
        if (_target == "ESP") {
            _filename.insert(0, "/localfs");
        } else if (_target == "SD") {
            _filename.insert(0, "/sd");
        } else {
            return;
        }
        macroMenu.addItem(new MacroItem { _name.c_str(), _filename });
    }

    void endDocument() override {
        current_scene->onFilesList();
        init_listener();
    }
} macroLinesListener;

class MacrocfgListener : public JsonListener {
private:
    std::string* _valuep;

    std::string _name;
    std::string _filename;
    std::string _target;

    int _level = 0;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override { macroMenu.removeAllItems(); }
    void startObject() override {
        if (++_level = 2) {
            _name.clear();
            _target.clear();
            _filename.clear();
        }
    }
    void key(const char* key) override {
        if (strcmp(key, "name") == 0) {
            _valuep = &_name;
            return;
        }
        if (strcmp(key, "filename") == 0) {
            _valuep = &_filename;
            return;
        }
        if (strcmp(key, "target") == 0) {
            _valuep = &_target;
            return;
        }
        _valuep = nullptr;
    }

    void value(const char* value) override {
        if (_valuep) {
            *_valuep = value;
        }
    }

    void endArray() override {
        // Otherwise this is the end
        current_scene->onFilesList();
        parser.setListener(pInitialListener);
    }
    void endObject() override {
        if (--_level = 1) {
            if (_target == "ESP") {
                _filename.insert(0, "/localfs");
            } else if (_target == "SD") {
                _filename.insert(0, "/sd");
            } else {
                return;
            }
            macroMenu.addItem(new MacroItem { _name.c_str(), _filename });
            return;
        }
    }

    void endDocument() override {}
} macrocfgListener;

class PreferencesListener : public JsonListener {
private:
    std::string* _valuep;

    std::string _name;
    std::string _filename;
    std::string _target;
    std::string _key;

    int  _level             = 0;
    bool _in_macros_section = false;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override {
        if (_in_macros_section) {
            macroMenu.removeAllItems();
        }
    }
    void endArray() override {
        if (_in_macros_section) {
            _in_macros_section = false;
            current_scene->onFilesList();
        }
    }

    void startObject() override { ++_level; }
    void key(const char* key) override {
        _key = key;
        if (_level < 2) {
            // The only thing we care about is the macros section at level 2
            return;
        }
        if (_level == 2 && (strcmp(key, "macros") == 0)) {
            _in_macros_section = true;
            return;
        }
        if (_in_macros_section) {
            if (strcmp(key, "action") == 0) {
                _valuep = &_filename;
                return;
            }
            if (strcmp(key, "type") == 0) {
                _valuep = &_target;
                return;
            }
            if (strcmp(key, "name") == 0) {
                _valuep = &_name;
                return;
            }
            // Ignore id, icon, and key fields
            _valuep = nullptr;
        }
    }

    void value(const char* value) override {
        if (_valuep) {
            *_valuep = value;
            _valuep  = nullptr;
        }
    }

    void endObject() override {
        --_level;
        if (_in_macros_section) {
            if (_target == "FS") {
                _filename.insert(0, "/localfs/");
            } else if (_target == "SD") {
                _filename.insert(0, "/sd/");
            } else if (_target == "CMD") {
                _filename.insert(0, "cmd:");
            } else {
                return;
            }
            macroMenu.addItem(new MacroItem { _name.c_str(), _filename });
            return;
        }
        if (_level == 0) {
            parser.setListener(pInitialListener);
        }
    }

    void endDocument() override {}
} preferencesListener;

JsonStreamingParser* macro_parser;

bool reading_macros = false;

void request_json_file(const char* name) {
    send_linef("$File/SendJSON=/%s", name);
    parser_needs_reset = true;
}

void request_macro_list_wu2() {
    //    reading_macros = true;
    request_json_file("macrocfg.json");
}
void request_macro_list_wu3() {
    request_json_file("preferences.json");
}

void try_next_macro_file(JsonListener* listener) {
    // We use schedule_action to avoid reentering
    // the parser code.
    if (!listener) {
        schedule_action(request_macro_list_wu2);
        return;
    }
    if (listener == &preferencesListener) {
        current_scene->onError("No Macros");
        return;
    }
    if (listener == &macrocfgListener) {
        schedule_action(request_macro_list_wu3);
    }
}
void request_macros() {
    try_next_macro_file(nullptr);
}

void init_macro_parser() {
    macro_parser = new JsonStreamingParser();
    macro_parser->setListener(&macroLinesListener);
}

void macro_parser_parse_line(const char* line) {
    char c;
    while ((c = *line++) != '\0') {
        macro_parser->parse(c);
    }
}

class FileLinesListener : public JsonListener {
private:
    bool _in_array;
    bool _key_is_error;
    bool _key_is_firstline = false;

public:
    void whitespace(char c) override {}

    void startDocument() override {}
    void startArray() override {
        if (reading_macros) {
            reading_macros = false;
            init_macro_parser();
            return;
        }
        fileLines.clear();
        _in_array = true;
    }
    void endArray() override {
        _in_array = false;
        if (macro_parser) {
            delete macro_parser;
            macro_parser = nullptr;
            parser.setListener(pInitialListener);
        }
        // init_listener();
    }

    void startObject() override {}

    void key(const char* key) override {
        if (strcmp(key, "firstline") == 0) {
            _key_is_firstline = true;
            return;
        }
    }

    void value(const char* value) override {
        if (macro_parser) {
            macro_parser_parse_line(value);
            return;
        }
        if (_in_array) {
            fileLines.push_back(value);
        }
        if (_key_is_firstline) {
            fileFirstLine = atoi(value);
        }
    }

    void endObject() override {
        parser.setListener(pInitialListener);
        current_scene->onFileLines(fileFirstLine, fileLines);
    }
    void endDocument() override {}
} fileLinesListener;

bool is_file(const char* str, const char* filename) {
    char* s = strstr(str, filename);
    return s && strlen(s) == strlen(filename);
}

class InitialListener : public JsonListener {
private:
    // Some keys are handled immediately and some have to wait
    // for the value.  key_t records the latter type.
    typedef enum {
        NONE,
        PATH,
        CMD,
        ARGUMENT,
        STATUS,
        ERROR,
    } key_t;

    key_t _key;

    std::string _cmd;
    std::string _argument;
    std::string _status;

    bool _is_json_file = false;

    JsonListener* _file_listener = nullptr;

public:
    void whitespace(char c) override {}
    void startDocument() override {
        _key          = NONE;
        _is_json_file = false;
        _status       = "ok";
    }
    void value(const char* value) override {
        switch (_key) {
            case PATH:
                // Old style json encapsulated in file lines array
                reading_macros = is_file(value, "macrocfg.json");
                break;
            case CMD:
                _cmd = value;
                if (strcmp(value, "$File/SendJSON") == 0) {
                    _is_json_file = true;
                }
                break;
            case ARGUMENT:
                _argument = value;
                if (_is_json_file) {
                    _is_json_file = false;
                    if (is_file(value, "macrocfg.json")) {
                        _file_listener = &macrocfgListener;
                    } else if (is_file(value, "preferences.json")) {
                        _file_listener = &preferencesListener;
                    } else {
                        _file_listener = nullptr;
                    }
                }
                break;
            case STATUS:
                _status = value;
                break;
            case ERROR:
                current_scene->onError(value);
                break;
        }
        _key = NONE;
    }

    void endArray() override {}
    void endObject() override { parser_needs_reset = true; }
    void endDocument() override {
        parser_needs_reset = true;
        if (_status != "ok" && _file_listener) {
            _status = "ok";
            try_next_macro_file(_file_listener);
        }
    }
    void startArray() override {}
    void startObject() override {}

    void key(const char* key) override {
        // Keys whose value is handled by a different listener
        if (strcmp(key, "files") == 0) {
            parser.setListener(&filesListListener);
            return;
        }
        if (strcmp(key, "file_lines") == 0) {
            parser.setListener(&fileLinesListener);
            return;
        }
        if (strcmp(key, "result") == 0) {
            if (_file_listener) {
                parser.setListener(_file_listener);
            }
            return;
        }

        // Keys where we must wait for the value
        if (strcmp(key, "path") == 0) {
            _key = PATH;
            return;
        }
        if (strcmp(key, "cmd") == 0) {
            _key = CMD;
            return;
        }
        if (strcmp(key, "argument") == 0) {
            _key = ARGUMENT;
            return;
        }
        if (strcmp(key, "status") == 0) {
            _key = STATUS;
            return;
        }
        if (strcmp(key, "error") == 0) {
            _key = ERROR;
            return;
        }
    }
} initialListener;

JsonListener* pInitialListener = &initialListener;

void init_listener() {
    parser.setListener(pInitialListener);
    parser_needs_reset = true;
}

void request_file_list(const char* dirname) {
    send_linef("$Files/ListGCode=%s", dirname);
    // parser.reset();
    parser_needs_reset = true;
}

void init_file_list() {
    init_listener();
    request_file_list("/sd");
    parser.reset();
}

void request_file_preview(const char* name, int firstline, int nlines) {
    reading_macros = false;
    send_linef("$File/ShowSome=%d:%d,%s", firstline, firstline + nlines, name);
    // parser.reset();
}

void parser_parse_line(const char* line) {
    char c;
    while ((c = *line++) != '\0') {
        parser.parse(c);
    }
}

extern "C" void handle_json(const char* line) {
    if (parser_needs_reset) {
        parser_needs_reset = false;
        parser.setListener(pInitialListener);
        parser.reset();
    }
    parser_parse_line(line);

#define Ack 0xB2
    fnc_realtime((realtime_cmd_t)Ack);
}

std::string wifi_mode;
std::string wifi_ssid;
std::string wifi_connected;
std::string wifi_ip;
// e.g. SSID=fooStatus=Connected:IP=192.168.0.67:MAC=40-F5-20-57-CE-64
void parse_wifi(char* arguments) {
    char* key = arguments;
    char* value;
    while (*key) {
        char* next;
        split(key, &next, ':');
        split(key, &value, '=');
        if (strcmp(key, "SSID") == 0) {
            wifi_ssid = value;
        } else if (strcmp(key, "Status") == 0) {
            wifi_connected = value;
        } else if (strcmp(key, "IP") == 0) {
            wifi_ip = value;
            // } else if (strcmp(key, "MAC") == 0) {
            //    mac = value;
        }
        key = next;
    }
}

// command is "Mode=STA" - or AP or No Wifi
void handle_radio_mode(char* command, char* arguments) {
    dbg_printf("Mode %s %s\n", command, arguments);
    char* value;
    split(command, &value, '=');
    wifi_mode = value;
    if (strcmp(value, "No Wifi") != 0) {
        parse_wifi(arguments);
        current_scene->reDisplay();
    }
}

extern "C" void handle_msg(char* command, char* arguments) {
    if (strcmp(command, "Homed") == 0) {
        char c;
        while ((c = *arguments++) != '\0') {
            const char* letters = "XYZABCUVW";
            char*       pos     = strchr(letters, c);
            if (pos) {
                set_axis_homed(pos - letters);
            }
        }
    }
    if (strcmp(command, "RST") == 0) {
        dbg_println("FluidNC Reset");
        state = Disconnected;
        act_on_state_change();
    }
    if (strcmp(command, "Files changed") == 0) {
        init_file_list();
    }
    if (strcmp(command, "JSON") == 0) {
        handle_json(arguments);
    }
    if (strncmp(command, "Mode=", strlen("Mode=")) == 0) {
        handle_radio_mode(command, arguments);
    }
}
