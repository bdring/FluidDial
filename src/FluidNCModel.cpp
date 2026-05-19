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
#include "BootLog.h"

#ifdef USE_WIFI
#    include "WiFiConnection.h"  // wifi_use_uart_mode()
#endif

extern Scene statusScene;

// local copies of status items
const char*        my_state_string    = "N/C";
state_t            state              = Disconnected;  // correct: we are disconnected until FluidNC responds
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

// Called once when status report received after being disconnected.
// Use schedule_action() to defer execution to dispatch_events() in the main loop where the parser is idle and _report is clean.
static void connect_init() {
    bootlog_printf("connected: state=%s", my_state_string);
    resetFlowControl();                  // clear any stale XOFF on the link
#ifndef USE_WIFI
    fnc_realtime((realtime_cmd_t)0x0c);  // Ctrl-L - echo off (UART only)
#endif
    send_line("$G");                     // Refresh GCode modes
    send_line("$RI=200");                // Enable auto-reporting every 200 ms
    init_file_list();                    // Request SD file list
    detect_homing_info();                // Probe axis homing state
}

extern "C" void show_state(const char* state_string) {
    previous_state = state;
    state_t new_state;
    if (decode_state_string(state_string, new_state) && state != new_state) {
        if (state == Disconnected) {
            schedule_action(connect_init);
        }
        state = new_state;
        if (state == Alarm && lastAlarm == 0) {  // Unknown
            send_line("$A");                     // Get last alarm
            awaiting_alarm = true;
        }
        act_on_state_change();
    }
}

extern "C" void handle_other(char* line) {
    // Intercept plain-JSON responses (FluidNC sends {"files":[...]} etc. without the [MSG:JSON:...] wrapper, sometimes split across frames).
    if (receive_plain_json(line)) {
        return;
    }
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
    request_redisplay();
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
    request_redisplay();
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
    request_redisplay();
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

// Counts consecutive disconnect_ms timeouts without RX, for the recovery ladder.
static int s_consecutive_timeouts = 0;

// Set by pendant_wait_for_fluidnc_ready() so fnc_is_connected() does not
// immediately fire a redundant ping right after a successful handshake.
static bool s_skip_first_ping = false;

void request_status_report() {
    fnc_putchar(0x11);           // XON; request software flow control
    fnc_realtime(StatusReport);  // Request fresh status
    next_ping_ms = milliseconds() + ping_interval_ms;
}

// Drain bytes until the RX line has been quiet for `quiet_ms`, OR until
// `max_total_ms` of wall time has elapsed (safety cap so a chatty
// peer can never wedge us). Bytes are discarded, NOT fed to the parser.
int flush_fnc_rx(uint32_t quiet_ms) {
    const uint32_t max_total_ms = 500;
    int            drained      = 0;
    uint32_t       start        = milliseconds();
    uint32_t       quiet_until  = start + quiet_ms;
    while ((int32_t)(milliseconds() - quiet_until) < 0) {
        if ((milliseconds() - start) >= max_total_ms) {
            break;
        }
        int c = fnc_getchar();
        if (c >= 0) {
            drained++;
            quiet_until = milliseconds() + quiet_ms;
        }
    }
    return drained;
}

// Probe FluidNC over UART until it answers, OR `budget_ms` elapses.
//
// IMPORTANT: bytes read here are DISCARDED, not fed to the parser.
// This function runs from setup() before activate_scene() has set
// current_scene, and many vendor parser callbacks (show_state, show_dro,
// show_alarm, show_error) eventually call current_scene methods. Feeding
// bytes to the parser before current_scene is set would crash on the
// first valid status report.
bool pendant_wait_for_fluidnc_ready(uint32_t budget_ms) {
    const uint32_t probe_window_ms = 500;
    const uint32_t probe_gap_ms    = 200;

    bootlog_printf("wait_ready: start budget=%lu", (unsigned long)budget_ms);

    int drained = flush_fnc_rx(50);
    bootlog_printf("wait_ready: drained=%d", drained);

    uint32_t deadline = milliseconds() + budget_ms;
    int      probe    = 0;
    int      total_rx = 0;
    while ((int32_t)(milliseconds() - deadline) < 0) {
        probe++;
        fnc_putchar(0x11);           // XON to clear any stale XOFF on FluidNC
        fnc_realtime(StatusReport);  // '?' probe

        int      window_rx  = 0;
        uint32_t window_end = milliseconds() + probe_window_ms;
        while ((int32_t)(milliseconds() - window_end) < 0) {
            int c = fnc_getchar();
            if (c >= 0) {
                window_rx++;  // discard byte; do not feed parser yet
            } else {
                delay_ms(2);
            }
        }
        total_rx += window_rx;
        if (window_rx > 0) {
            bootlog_printf("wait_ready: alive probe=%d bytes=%d", probe, window_rx);
            s_skip_first_ping = true;
            return true;
        }
        bootlog_printf("wait_ready: probe %d silent", probe);
        delay_ms(probe_gap_ms);
    }
    bootlog_printf("wait_ready: timeout probes=%d rx=%d", probe, total_rx);
    return false;
}

// Escalating recovery when the link has gone silent. Counter ticks once
// per disconnect_interval_ms with no RX.
//   tick 1: drain stale RX and send XON + status request.
//   tick 3: re-initialize the UART driver from scratch, then re-probe.
//           UART re-init is a no-op when running in WiFi mode.
static void recover_link(int tick) {
    bootlog_printf("recover: tick=%d", tick);
    flush_fnc_rx(20);
    request_status_report();
    if (tick == 3) {
#ifdef USE_WIFI
        if (wifi_use_uart_mode()) {
            bootlog_printf("recover: re-init uart");
            reinit_fnc_uart();
            request_status_report();
        }
#else
        bootlog_printf("recover: re-init uart");
        reinit_fnc_uart();
        request_status_report();
#endif
    }
}

bool fnc_is_connected() {
#ifdef DEV_SIMULATED_CONNECT
    return true;
#endif
    int now = milliseconds();
    if (starting) {
        starting      = false;
        disconnect_ms = now + (disconnect_interval_ms - ping_interval_ms);
        if (s_skip_first_ping) {
            // Handshake already got a response; let the regular ping cadence resume.
            s_skip_first_ping = false;
            next_ping_ms      = now + ping_interval_ms;
        } else {
            request_status_report();  // sets next_ping_ms
        }
        return false;             // Do we need a value for "unknown"?
    }
    if ((now - disconnect_ms) >= 0) {
        s_consecutive_timeouts++;
        bootlog_printf("disconnected: timeout #%d", s_consecutive_timeouts);
        recover_link(s_consecutive_timeouts);
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
    s_consecutive_timeouts = 0;
}
