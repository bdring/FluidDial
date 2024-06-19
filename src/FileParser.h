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
