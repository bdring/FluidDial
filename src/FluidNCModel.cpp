// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FluidNCModel.h"
#include "ConfigItem.h"
#include "FileParser.h"  // init_file_list()
#include <map>
#include "System.h"
#include "Scene.h"
#include "e4math.h"
#include "HomingScene.h"

extern Scene statusScene;

// local copies of status items
const char*        my_state_string    = "N/C";
state_t            state              = Idle;
int                n_axes             = 3;
pos_t              myAxes[6]          = { 0 };
bool               myLimitSwitches[6] = { false };
bool               myProbeSwitch      = false;
const char*        myFile             = "";  // running SD filename
const char*        myCtrlPins         = "";
file_percent_t     myPercent          = 0.0;  // percent conplete of SD file
override_percent_t myFro              = 100;  // Feed rate override
override_percent_t mySro              = 100;  // Spindle Override
uint32_t           myFeed             = 0;
uint32_t           mySpeed            = 0;
uint32_t           mySelectedTool     = 0;

std::string myModes = "no data";

int      lastAlarm = 0;
int      lastError = 0;
bool     inInches  = false;
uint32_t errorExpire;

int num_digits() {
    return inInches ? 3 : 2;
}

// clang-format off
// Maps the state strings in status reports to internal state enum values
struct cmp_str {
   bool operator()(char const *a, char const *b) const    {
      return strcmp(a, b) < 0;
   }
};

std::map<const char *, state_t, cmp_str>  state_map = {
    { "Idle", Idle },
    { "Alarm", Alarm },
    { "Hold:0", Hold },
    { "Hold:1", Hold },
    { "Run", Cycle },
    { "Jog", Jog },
    { "Home", Homing },
    { "Door:0", DoorClosed },
    { "Door:1", DoorOpen },
    { "Check", CheckMode },
    { "Sleep", GrblSleep },
};
// clang-format on

bool decode_state_string(const char* state_string, state_t& state) {
    if (strcmp(my_state_string, state_string) != 0) {
        auto found = state_map.find(state_string);
        if (found != state_map.end()) {
            my_state_string = found->first;
            state           = found->second;
            return true;
        }
    }
    return false;
}

void set_disconnected_state() {
    state           = Disconnected;
    my_state_string = "N/C";
}

// clang-format off
std::map<int, const char*> error_map = {  // Do here so abreviations are right for the dial
    { 0, "None"},
    { 1, "GCode letter"},
    { 2, "GCode format"},
    { 3, "Bad $ command"},
    { 4, "Negative value"},
    { 5, "Setting Diabled"},
    { 10, "Soft limit error"},
    { 13, "Check door"},
    { 18, "No Homing Cycles"},
    { 20, "Unsupported GCode"},
    { 22, "Undefined feedrate"},
    { 19, "No single axis"},
    { 34, "Arc radius error"},
    { 39, "P Param Exceeded"},
};
// clang-format on

const char* decode_error_number(int error_num) {
    if (error_map.find(error_num) != error_map.end()) {
        return error_map[error_num];
    }
    static char retval[33];
    sprintf(retval, "%d", error_num);
    return retval;
}

extern "C" void begin_status_report() {
    myPercent = 0;
}

extern "C" void show_file(const char* filename, file_percent_t percent) {
    myPercent = percent;
}

extern "C" void show_overrides(override_percent_t feed_ovr, override_percent_t rapid_ovr, override_percent_t spindle_ovr) {
    myFro = feed_ovr;
    mySro = spindle_ovr;
}

extern "C" void show_feed_spindle(uint32_t feedrate, uint32_t spindle_speed) {
    myFeed  = feedrate;
    mySpeed = spindle_speed;
};

extern "C" void show_limits(bool probe, const bool* limits, size_t n_axis) {
    myProbeSwitch = probe;
    memcpy(myLimitSwitches, limits, n_axis * sizeof(*limits));
}

extern "C" void show_control_pins(const char* pins) {
    //dbg_printf("show_control_pins:%s\r\n", pins);
    myCtrlPins = pins;
}

#ifdef E4_POS_T
extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    n_axes = (int)n_axis;
    for (int axis = 0; axis < n_axis; axis++) {
        e4_t axis_val = axes[axis];
        if (isMpos) {
            axis_val -= wco[axis];
        }
        myAxes[axis] = inInches ? e4_mm_to_inch(axis_val) : axis_val;
    }
}
#else
pos_t fromMm(pos_t position) {
    return inInches ? position / 25.4 : position;
}
pos_t toMm(pos_t position) {
    return inInches ? position * 25.4 : position;
}

extern "C" void show_dro(const pos_t* axes, const pos_t* wco, bool isMpos, bool* limits, size_t n_axis) {
    for (int axis = 0; axis < n_axis; axis++) {
        myAxes[axis] = fromMm(axes[axis]);
        if (isMpos) {
            myAxes[axis] -= fromMm(wco[axis]);
        }
    }
}
#endif

void send_line(const char* s, int timeout) {
    fnc_send_line(s, timeout);
    dbg_println(s);
}
static void vsend_linef(const char* fmt, va_list va) {
    static char buf[128];
    vsnprintf(buf, 128, fmt, va);
    send_line(buf);
}
void send_linef(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsend_linef(fmt, args);
    va_end(args);
}

char axisNumToChar(int axis) {
    return "XYZABC"[axis];
}

const char* axisNumToCStr(int axis) {
    static char ret[2] = { '\0', '\0' };
    ret[0]             = axisNumToChar(axis);
    return ret;
}

const char* intToCStr(int val) {
    static char buffer[20];
    sprintf(buffer, "%d", val);
    return buffer;
}

const char* mode_string() {
    return myModes.c_str();
}

state_t previous_state;
bool    awaiting_alarm = false;

extern "C" void show_state(const char* state_string) {
    previous_state = state;
    state_t new_state;
    if (decode_state_string(state_string, new_state) && state != new_state) {
        if (state == Disconnected) {
            send_line("$G");  // Refresh GCode modes
            send_line("$RI=200");
            init_file_list();
            detect_homing_info();
        }
        state = new_state;
        if (state == Alarm && lastAlarm == 0) {  // Unknown
            send_line("$A");                     // Get last alarm
            awaiting_alarm = true;
            return;
        }
        act_on_state_change();
    }
}

extern "C" void handle_other(char* line) {
    if (*line == '$') {
        parse_dollar(line);
        return;
    }
    int alarmlen = strlen("Active alarm: ");
    if (strncmp(line, "Active alarm: ", alarmlen) == 0) {
        lastAlarm = atoi(line + alarmlen);
        if (awaiting_alarm) {
            dbg_printf("Got alarm %d\n", lastAlarm);
            awaiting_alarm = false;
            act_on_state_change();
        }
    }
}

extern "C" void show_error(int error) {
    errorExpire = milliseconds() + 1000;
    lastError   = error;
    current_scene->reDisplay();
}

extern "C" void show_timeout() {
    dbg_println("Timeout");
}
extern "C" void show_ok() {}

extern "C" void end_status_report() {
    current_scene->onDROChange();
}

extern "C" void show_alarm(int alarm) {
    lastAlarm = alarm;
    current_scene->reDisplay();
}

extern "C" void show_gcode_modes(struct gcode_modes* modes) {
    inInches = strcmp(modes->units, "In") == 0 || strcmp(modes->units, "G20") == 0;

    myModes = modes->wcs;
    myModes += " ";
    myModes += modes->units;
    myModes += " ";
    myModes += modes->distance;
    myModes += " ";
    myModes += modes->spindle;
    if (strcmp(modes->mist, "On") == 0) {
        myModes += " Mist";
    }
    if (strcmp(modes->flood, "On") == 0) {
        myModes += " Flood";
    }

    mySelectedTool = modes->tool;
    current_scene->reDisplay();
}

int disconnect_ms = 0;
int next_ping_ms  = 0;

// If we haven't heard from FluidNC in 4 seconds for some other reason,
// send a status report request.
const int ping_interval_ms = 4000;

// If we haven't heard from FluidNC in 6 seconds for any reason, declare
// FluidNC unresponsive.  After a ping, FluidNC has 2 seconds to respond.
const int disconnect_interval_ms = 6000;

bool starting = true;

void request_status_report() {
    fnc_putchar(0x11);           // XON; request software flow control
    fnc_realtime(StatusReport);  // Request fresh status
    next_ping_ms = milliseconds() + ping_interval_ms;
}

bool fnc_is_connected() {
    int now = milliseconds();
    if (starting) {
        starting      = false;
        disconnect_ms = now + (disconnect_interval_ms - ping_interval_ms);
        request_status_report();  // sets next_ping_ms
        return false;             // Do we need a value for "unknown"?
    }
    if ((now - disconnect_ms) >= 0) {
        next_ping_ms  = now + ping_interval_ms;
        disconnect_ms = now + disconnect_interval_ms;
        return false;
    }

    if ((now - next_ping_ms) >= 0) {
        request_status_report();
    }
    return true;
}

void update_rx_time() {
    int now       = milliseconds();
    next_ping_ms  = now + ping_interval_ms;
    disconnect_ms = now + disconnect_interval_ms;
}
