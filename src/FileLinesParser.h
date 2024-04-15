// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

typedef void (*callback_t)(void*);

#include <string>
#include <vector>

extern std::string dirName;
extern int         dirLevel;

extern void request_file_list();
extern void request_macro_list();

extern std::vector<std::string> fileLines;

extern void request_file_preview(const char* name);

extern std::string current_filename;

void init_listener();
void init_file_list();

void        enter_directory(const char* dirname);
inline void enter_directory(const std::string& dirname) {
    enter_directory(dirname.c_str());
}
void exit_directory();
