// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <string>
#include <vector>

typedef void (*callback_t)(void*);

struct fileinfo {
    std::string fileName;
    int         fileSize;
    bool        isDir() const { return fileSize < 0; }
};

extern fileinfo              fileInfo;
extern std::vector<fileinfo> fileVector;

extern void request_file_list(const char* dirname);

struct Macro {
    std::string name;
    std::string filename;
    std::string target;
};

extern std::vector<Macro*> macros;

extern void request_macros();

extern void request_file_preview(const char* name, int firstline, int lastline);

extern std::string current_filename;
extern std::string wifi_mode, wifi_ip, wifi_connected, wifi_ssid;

void init_listener();
void init_file_list();

// True while the streaming JSON parser is mid-document (outer-brace
// depth > 0). Used by handle_other() in FluidNCModel.cpp to route
// continuation chunks of a multi-line response back to handle_json().
bool json_in_progress();

// Discard any in-flight JSON state. Call when a transport-level event
// (socket close, "ok"/"error:" delimiter, $-response) ends the document
// abruptly so the next legitimate document starts with a clean parser.
void json_reset_depth();

// Called from show_error when FluidNC returns a bare "error:N" (no JSON
// wrapper, as Telnet does). Advances the macro file chain so the
// "Reading Macros" UI doesn't hang when a $File/SendJSON request was
// rejected. No-op if no file request is currently in flight.
extern "C" void file_request_failed_advance();
