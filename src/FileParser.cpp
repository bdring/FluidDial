// Copyright (c) 2023 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FileParser.h"

#include "Scene.h"  // request_redisplay()
#include "Menu.h"
#include "GrblParserC.h"  // send_line()
#include "HomingScene.h"  // set_axis_homed()

#include <JsonStreamingParser.h>
#include <JsonListener.h>

#include "MacroItem.h"

extern Menu macroMenu;

#ifdef FNC_RX_TRACE
#    include <stdarg.h>
// Trace output is buffered in RAM and flushed only when the link is idle.
// Printing inline is self-defeating: dbg_print blocks waiting for USB buffer
// space, and 50 ms of blocking at 1 Mbaud is ~5 KB of arriving UART data --
// more than the RX ring holds. The act of tracing was overflowing the ring and
// destroying the very transfer being traced.
static char   s_trace_buf[4096];
static size_t s_trace_len     = 0;
static int    s_trace_dropped = 0;

static void trace_defer(const char* fmt, ...) {
    char    line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    size_t len = strlen(line);
    if (s_trace_len + len + 1 >= sizeof(s_trace_buf)) {
        ++s_trace_dropped;
        return;
    }
    memcpy(s_trace_buf + s_trace_len, line, len);
    s_trace_len += len;
}

static void trace_flush() {
    if (s_trace_len == 0 && s_trace_dropped == 0) {
        return;
    }
    s_trace_buf[s_trace_len] = '\0';
    dbg_print(s_trace_buf);
    if (s_trace_dropped) {
        dbg_printf("[trace] %d line(s) dropped, buffer full\n", s_trace_dropped);
    }
    s_trace_len     = 0;
    s_trace_dropped = 0;
}
#endif

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
    // _level at which the "macros" key appeared. Entries are the objects one
    // deeper than this; anything deeper still is nested structure inside an
    // entry and must not be mistaken for one.
    int _macros_level = -1;

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
            _macros_level      = -1;
#ifdef FNC_RX_TRACE
            trace_defer("[prefs] <<< macros section ends, %d item(s) added\n", macroMenu.num_items());
#endif
            current_scene->onFilesList();
        }
    }

    void startObject() override {
        ++_level;
        // Reset per-entry state. Without this the fields carry over from the
        // previous macro, so an entry missing "action" inherits the last one's
        // filename -- which has ALREADY had its "/localfs/" prefix inserted --
        // and gets prefixed a second time, surfacing as "//localfs/". An entry
        // missing "type" likewise inherits the previous type and gets added
        // when it should have been skipped. The other two listeners already do
        // this; this one was the odd one out.
        if (_in_macros_section && _level == _macros_level + 1) {
            _name.clear();
            _target.clear();
            _filename.clear();
        }
    }
    void key(const char* key) override {
        _key = key;
#ifdef FNC_RX_TRACE
        // Only the top level and the macros section. Printing every key was
        // self-defeating: dbg output blocks waiting for USB buffer space, and
        // several hundred lines through the settings/keymap blobs stalled the
        // UART reader long enough to drop the very bytes being traced.
        if (_level <= 1 || _in_macros_section) {
            trace_defer("[prefs] L%d key=%s\n", _level, key);
        }
#endif
        // Match the macros key at whatever depth it appears. This listener is
        // installed part-way through the document (on the "result" key of the
        // $File/SendJSON wrapper), so its _level is relative to wherever it
        // took over -- preferences.json puts "settings"/"macros" at _level 1
        // here, not 2. Pinning the check to _level == 2 meant the earlier
        // _level < 2 bail swallowed the macros key before it was ever tested.
        if (strcmp(key, "macros") == 0) {
            _in_macros_section = true;
            _macros_level      = _level;
#ifdef FNC_RX_TRACE
            trace_defer("[prefs] >>> macros section begins at L%d\n", _level);
#endif
            return;
        }
        if (_level < 2) {
            return;
        }
        if (_in_macros_section) {
            // WebUI versions disagree on the spelling: older exports use
            // filename/target (what MacroListListener expects), newer ones use
            // action/type. Accept either rather than silently producing an
            // empty entry when the file uses the other one.
            if (strcmp(key, "action") == 0 || strcmp(key, "filename") == 0) {
                _valuep = &_filename;
                return;
            }
            if (strcmp(key, "type") == 0 || strcmp(key, "target") == 0) {
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
        if (_in_macros_section && _level == _macros_level) {
#ifdef FNC_RX_TRACE
            trace_defer("[prefs] macro name=\"%s\" type=\"%s\" action=\"%s\"\n",
                       _name.c_str(), _target.c_str(), _filename.c_str());
#endif
            // An entry with nothing to run is not a macro. Adding it yields a
            // menu row that does nothing, or sends a bare command line.
            if (_filename.empty()) {
#ifdef FNC_RX_TRACE
                trace_defer("[prefs] ^^ DROPPED: empty action\n");
#endif
                return;
            }
            // Normalise before prefixing. One schema stores "foo.g", the
            // other "/foo.g"; blindly inserting "/localfs/" turns the latter
            // into "/localfs//foo.g".
            if (_target == "FS" || _target == "ESP" || _target == "SD") {
                if (!_filename.empty() && _filename[0] == '/') {
                    _filename.erase(0, 1);
                }
            }
            if (_target == "FS" || _target == "ESP") {
                _filename.insert(0, "/localfs/");
            } else if (_target == "SD") {
                _filename.insert(0, "/sd/");
            } else if (_target == "CMD") {
                _filename.insert(0, "cmd:");
            } else {
#ifdef FNC_RX_TRACE
                // Every entry whose type isn't one of the three recognised
                // spellings is silently discarded -- the array parses fine and
                // the menu still ends up empty, which looks identical to a
                // failed transfer.
                trace_defer("[prefs] ^^ DROPPED: unrecognised type \"%s\"\n", _target.c_str());
#endif
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

// When the in-flight $File/SendJSON was issued, so a transfer that dies on the
// wire can't strand the macro chain. Zero means nothing is outstanding.
static uint32_t           s_file_request_sent_ms   = 0;
static constexpr uint32_t FILE_REQUEST_TIMEOUT_MS = 5000;
// How long after the last request, with no document in flight, before auto
// reporting is turned back on.
static constexpr uint32_t AUTO_REPORT_RESUME_MS = 1000;

// FluidNC's auto-report ($RI) keeps emitting <Idle|MPos:...> status lines while
// a file is streaming, on the same channel. Any byte lost to a stall then welds
// the two together -- a chunk ends up carrying the tail of a status report
// ("...|FS:0,0>") and the JSON parser derails. Nothing on the macro path needs
// live DRO, so silence auto-reporting for the duration of the transfer.
static bool s_auto_report_suspended = false;

static void suspend_auto_report() {
    if (!s_auto_report_suspended) {
        s_auto_report_suspended = true;
        send_line("$RI=0");
    }
}

void resume_auto_report() {
    if (s_auto_report_suspended) {
        s_auto_report_suspended = false;
        send_line("$RI=200");
    }
}

void request_json_file(const char* name) {
    suspend_auto_report();
    send_linef("$File/SendJSON=/%s", name);
    parser_needs_reset     = true;
    s_file_request_sent_ms = milliseconds();
}

// Track which file request is in flight so we can advance the macro
// chain if FluidNC rejects it with a bare "error:N" (Telnet's terse
// error form, no JSON wrapper). Without this, a rejection on
// $File/SendJSON=macrocfg.json never unblocks the chain and the macro
// menu sits forever on "Reading Macros". Cleared either via the success
// path (try_next_macro_file from JSON endDocument) or via the failure
// path (file_request_failed_advance from show_error).
static JsonListener* s_pending_file_listener = nullptr;

// Cooldown millisecond stamp from the last chain advance. FluidNC over
// Telnet can emit a bare "error:N" for a failed file request AFTER it
// has already wrapped that failure in a JSON-status response — by then
// the chain has advanced to the next file and the stray error byte
// would falsely advance again. Within this window the second advance
// is suppressed.
static uint32_t s_chain_advance_at_ms = 0;
static constexpr uint32_t CHAIN_ADVANCE_COOLDOWN_MS = 250;

void request_macro_list_wu2() {
#ifdef FNC_RX_TRACE
    dbg_printf("[macro-chain] request macrocfg.json (wu2)\n");
#endif
    s_pending_file_listener = &macrocfgListener;
    request_json_file("macrocfg.json");
}
void request_macro_list_wu3() {
#ifdef FNC_RX_TRACE
    dbg_printf("[macro-chain] request preferences.json (wu3)\n");
#endif
    s_pending_file_listener = &preferencesListener;
    request_json_file("preferences.json");
}

void try_next_macro_file(JsonListener* listener) {
    s_pending_file_listener = nullptr;
    s_chain_advance_at_ms   = milliseconds();
    // We use schedule_action to avoid reentering
    // the parser code.
    if (!listener) {
#ifdef FNC_RX_TRACE
        dbg_printf("[macro-chain] start -> wu2\n");
#endif
        schedule_action(request_macro_list_wu2);
        return;
    }
    if (listener == &preferencesListener) {
#ifdef FNC_RX_TRACE
        dbg_printf("[macro-chain] preferences exhausted -> No Macros\n");
#endif
        current_scene->onError("No Macros");
        return;
    }
    if (listener == &macrocfgListener) {
#ifdef FNC_RX_TRACE
        dbg_printf("[macro-chain] macrocfg miss -> wu3\n");
#endif
        schedule_action(request_macro_list_wu3);
    }
}

// Called from show_error in FluidNCModel.cpp. If a macrocfg.json or
// preferences.json request is in flight when FluidNC returns a bare
// error:N (no JSON wrapper), advance the chain as if endDocument had
// fired with non-ok status. Without this the macro chain hangs forever
// on the "Reading Macros" screen when Telnet rejects $File/SendJSON.
//
// Suppress when json_in_progress(): FluidNC interleaves messages on the
// wire, so an error:N from a previously-failed request can arrive AFTER
// the next request's JSON has started streaming. Advancing in that case
// kills a healthy in-flight document and shows "No Macros" even though
// the macros are right there in the buffer.
extern "C" void file_request_failed_advance() {
    if (json_in_progress()) {
#ifdef FNC_RX_TRACE
        trace_defer("[macro-chain] stale error suppressed (JSON in flight)\n");
#endif
        return;
    }
    // Suppress stray error bytes that arrive immediately after a
    // chain advance — they belong to the request we just transitioned
    // away from, not the one that's now pending.
    if (s_chain_advance_at_ms != 0 &&
        (milliseconds() - s_chain_advance_at_ms) < CHAIN_ADVANCE_COOLDOWN_MS) {
#ifdef FNC_RX_TRACE
        trace_defer("[macro-chain] stale error suppressed (advance cooldown)\n");
#endif
        return;
    }
    JsonListener* l = s_pending_file_listener;
    if (l) {
#ifdef FNC_RX_TRACE
        dbg_printf("[macro-chain] file request failed, advancing from %s\n",
                   l == &macrocfgListener ? "macrocfg" :
                   l == &preferencesListener ? "preferences" : "?");
#endif
        s_pending_file_listener = nullptr;
        try_next_macro_file(l);
    }
}

void request_macros() {
    try_next_macro_file(nullptr);
}

// Called from dispatch_events(). show_error() deliberately ignores an error:N
// that lands while a document is streaming, which is right for a stale error
// but means a genuinely torn transfer never advances the chain -- the menu then
// sits on "Reading Macros" forever. Give the request a deadline instead.
void service_macro_chain() {
    // Restore auto-reporting once nothing is streaming any more. Keying this on
    // "no document in flight AND the last request is old enough" rather than on
    // the terminal branches means the DRO comes back down every route home --
    // completion, error, give-up -- and for the file list and preview transfers
    // too, which have no chain state of their own.
    if (s_auto_report_suspended && !s_pending_file_listener && !json_in_progress() &&
        (uint32_t)(milliseconds() - s_file_request_sent_ms) >= AUTO_REPORT_RESUME_MS) {
        resume_auto_report();
    }
#ifdef FNC_RX_TRACE
    // Flush only when nothing is streaming, so the (blocking) USB writes can
    // never stall the UART reader mid-document.
    if (!json_in_progress()) {
        trace_flush();
    }
#endif
    if (!s_pending_file_listener) {
        return;
    }
    if ((uint32_t)(milliseconds() - s_file_request_sent_ms) < FILE_REQUEST_TIMEOUT_MS) {
        return;
    }
#ifdef FNC_RX_TRACE
    dbg_printf("[macro-chain] request timed out, advancing\n");
#endif
    JsonListener* l         = s_pending_file_listener;
    s_pending_file_listener = nullptr;
    json_reset_depth();
    try_next_macro_file(l);
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
    const char* s = strstr(str, filename);
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
    suspend_auto_report();
    s_file_request_sent_ms = milliseconds();
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
    suspend_auto_report();
    s_file_request_sent_ms = milliseconds();
    send_linef("$File/ShowSome=%d:%d,%s", firstline, firstline + nlines, name);
    // parser.reset();
}

// ── Streaming JSON receiver ───────────────────────────────────────────────────
// Large payloads can arrive split across TCP segments (the Telnet RX refill is
// line-terminated) so GrblParserC delivers them as separate lines to
// handle_other(). Rather than accumulating the whole document into a std::string
// (which fragments the heap for big file lists), feed each chunk into the
// streaming JSON parser as it arrives and track outer-brace depth so we know
// when a document is complete and when the parser is safe to reset.

static int  s_json_depth  = 0;
static bool s_json_in_str = false;
static bool s_json_esc    = false;

bool json_in_progress() {
    return s_json_depth > 0;
}

void json_reset_depth() {
    s_json_depth       = 0;
    s_json_in_str      = false;
    s_json_esc         = false;
    parser_needs_reset = true;
}

// Feed a chunk into the parser one char at a time, counting outer-brace
// depth as we go. Tracks string state (incl. backslash escapes) across
// chunk boundaries so '{' and '}' inside JSON string values — for
// example a macro action like "G92 Z{z}" — don't inflate the counter
// and prevent the document from ever returning to depth 0.
static void parser_feed_line(const char* line) {
    char c;
    while ((c = *line++) != '\0') {
        if (s_json_esc) {
            s_json_esc = false;
        } else if (s_json_in_str) {
            if (c == '\\') {
                s_json_esc = true;
            } else if (c == '"') {
                s_json_in_str = false;
            }
        } else {
            if (c == '"') {
                s_json_in_str = true;
            } else if (c == '{') {
                s_json_depth++;
            } else if (c == '}') {
                if (s_json_depth > 0) {
                    s_json_depth--;
                }
            }
        }
        parser.parse(c);
    }
}

extern "C" void handle_json(const char* line) {
#ifdef FNC_RX_TRACE
    // Print depth + the leading 60 chars of the chunk so we can SEE the
    // wire format. Truncated to avoid drowning the monitor on large
    // documents.
    size_t len = strlen(line);
    char   peek[61];
    size_t pn = len < 60 ? len : 60;
    memcpy(peek, line, pn);
    peek[pn] = '\0';
    trace_defer("[json] len=%u d=%d | %s%s\n", (unsigned)len, s_json_depth,
               peek, len > 60 ? "..." : "");
#endif
    // Only reset the parser at a document boundary, never mid-stream — a reset
    // in the middle of a multi-chunk document loses the in-flight state and
    // the macro list comes back empty.
    if (parser_needs_reset && s_json_depth == 0) {
        parser_needs_reset = false;
        parser.setListener(pInitialListener);
        parser.reset();
    }
    parser_feed_line(line);

    // No per-chunk ack is sent. Restoring the 0xB2 that aeddaa9 removed was
    // tried here and made things worse: overlapping fragments appeared in the
    // stream ("false" arriving as "falalse"), i.e. the sender re-emitting across
    // a chunk boundary. The byte loss that motivated the experiment was really
    // the WiFi stack starving the UART reader in poll_extra(), which is fixed
    // separately. FluidNC's $File/SendJSON does not need the ack.
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
//
// FluidNC re-emits [MSG:Mode=...] periodically as a heartbeat. Forcing a
// full reDisplay every time hammers the heap (every PNG decode allocates
// & frees, and the LGFX/pngle allocator fragments over time -> mDNS OOMs
// after a minute). We now only reDisplay when something the UI shows has
// actually changed.
void handle_radio_mode(char* command, char* arguments) {
    char* value;
    split(command, &value, '=');

    static std::string last_mode;
    static std::string last_ssid;
    static std::string last_connected;
    static std::string last_ip;

    wifi_mode = value;
    if (strcmp(value, "No Wifi") == 0) {
        if (last_mode != wifi_mode) {
            last_mode = wifi_mode;
            request_redisplay();
        }
        return;
    }

    parse_wifi(arguments);

    if (last_mode != wifi_mode || last_ssid != wifi_ssid || last_connected != wifi_connected || last_ip != wifi_ip) {
        last_mode      = wifi_mode;
        last_ssid      = wifi_ssid;
        last_connected = wifi_connected;
        last_ip        = wifi_ip;
        dbg_printf("Mode %s %s\n", command, arguments);
        request_redisplay();
    }
}

extern "C" void handle_msg(char* command, char* arguments) {
    if (strcmp(command, "Homed") == 0) {
        char c;
        while ((c = *arguments++) != '\0') {
            const char* letters = "XYZABCUVW";
            const char* pos     = strchr(letters, c);
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
        schedule_action(init_file_list);
    }
    if (strcmp(command, "JSON") == 0) {
        handle_json(arguments);
    }
    if (strncmp(command, "Mode=", strlen("Mode=")) == 0) {
        handle_radio_mode(command, arguments);
    }
}
