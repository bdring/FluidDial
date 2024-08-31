// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for Windows

// stdio.h must precede the include of M5Unified.h in System.h
// in order for image files to work correctly
#include "stdio.h"

#include "System.h"
#include "FluidNCModel.h"
#include "Drawing.h"
#include "NVS.h"
#include "Area.h"
#include "VMenu.h"

#define LGFX_AUTODETECT
#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>

#include <windows.h>
#include <commctrl.h>
#include <direct.h>

m5::Touch_Class  xtouch;
m5::Touch_Class& touch = xtouch;
LGFX             xdisplay(800, 480, 1);
LGFX_Device&     display = xdisplay;

bool round_display = false;

const int n_buttons    = 3;
const int button_w     = 160;
const int button_h     = 160;
const int button_inset = 20;
const int scene_wh     = 240;
const int scene_inset  = 80;

const int stripe_w = 800 - 240;

#if 0
Area xscene_area(&xdisplay, 16, scene_inset, 0, scene_wh, scene_wh);

Area vmenu_area(&xdisplay, 16, 0, 0, 70, 480);

//LGFX_Sprite button_sprite(xdisplay);
// Area        button_area(&button_sprite, scene_inset + scene_wh, 0, button_w, scene_wh);
#else
// clang-format off
Area        xscene_area0(&xdisplay, 16, scene_inset,         0, scene_wh, scene_wh);
Area        xscene_area1(&xdisplay, 16, scene_inset + 240,   0, scene_wh, scene_wh);
Area        xscene_area2(&xdisplay, 16, scene_inset + 480,   0, scene_wh, scene_wh);
Area        xscene_area3(&xdisplay, 16, scene_inset,       240, scene_wh, scene_wh);
Area        xscene_area4(&xdisplay, 16, scene_inset + 240, 240, scene_wh, scene_wh);
Area        xscene_area5(&xdisplay, 16, scene_inset + 480, 240, scene_wh, scene_wh);

Area vmenu_area(&xdisplay, 16, 0, 0, 70, 480);
// clang-format on
#endif

constexpr const int button_x = 800 - button_w;

Area button_areas[3] = {
    { nullptr, 0, button_x, 0 * button_h, button_w, button_h },
    { nullptr, 0, button_x, 1 * button_h, button_w, button_h },
    { nullptr, 0, button_x, 2 * button_h, button_w, button_h },
};

int button_colors[] = { RED, YELLOW, GREEN };

void system_background(Area* area) {
    area->drawOutlinedRect(0, 0, area->w(), area->h(), BLACK, WHITE);
}

extern "C" int milliseconds() {
    return lgfx::millis();
}

void delay_ms(uint32_t ms) {
    SDL_Delay(ms);
}

void Area::drawPngFile(const char* filename, int x, int y) {
    std::string fn("data/");
    fn += filename;
    // When datum is middle_center, the origin is the center of the area and the
    // +Y direction is down.
    sprite()->drawPngFile(fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

#define TIOCM_LE 0x001
#define TIOCM_DTR 0x002
#define TIOCM_RTS 0x004
#define TIOCM_ST 0x008
#define TIOCM_SR 0x010
#define TIOCM_CTS 0x020
#define TIOCM_CAR 0x040
#define TIOCM_RNG 0x080
#define TIOCM_DSR 0x100
#define TIOCM_CD TIOCM_CAR
#define TIOCM_RI TIOCM_RNG

static bool getcomm(HANDLE comfid, LPDCB dcb) {
    if (!GetCommState((HANDLE)comfid, dcb)) {
        printf("Can't get COM mode, error %d\n", GetLastError());
        return true;
    }
    return false;
}

static bool setcomm(HANDLE comfid, LPDCB dcb) {
    if (!SetCommState((HANDLE)comfid, dcb)) {
        printf("Can't set COM mode, error %d\n", GetLastError());
        return true;
    }
    return false;
}

bool serial_set_parity(HANDLE comfid, char parity)  // 'n', 'e', 'o', 'm', 's'
{
    DCB dcb;
    if (getcomm(comfid, &dcb)) {
        return true;
    }
    switch (parity) {
        case 'n':
            dcb.fParity = 0;
            dcb.Parity  = NOPARITY;
            break;
        case 'o':
            dcb.fParity = 1;
            dcb.Parity  = ODDPARITY;
            break;
        case 'e':
            dcb.fParity = 1;
            dcb.Parity  = EVENPARITY;
            break;
        case 'm':
            dcb.fParity = 1;
            dcb.Parity  = MARKPARITY;
            break;
        case 's':
            dcb.fParity = 1;
            dcb.Parity  = SPACEPARITY;
            break;
    }
    return setcomm(comfid, &dcb);
}

int serial_set_modem_control(HANDLE comfid, bool rts, bool dtr) {
    DCB dcb;
    if (getcomm(comfid, &dcb)) {
        return -1;
    }

    int modemstatold = 0;
    if (dcb.fDtrControl == DTR_CONTROL_ENABLE) {
        modemstatold |= TIOCM_DTR;
    }
    if (dcb.fRtsControl == RTS_CONTROL_ENABLE) {
        modemstatold |= TIOCM_RTS;
    }

    dcb.fDtrControl = dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;

    (void)setcomm(comfid, &dcb);
    return modemstatold;
}

int serial_get_modem_control(HANDLE comfid) {
    DWORD ModemStat;
    if (!GetCommModemStatus(comfid, &ModemStat)) {
        return 0;
    }
    int retval = 0;
    if (ModemStat & MS_CTS_ON) {
        retval |= TIOCM_CTS;
    }
    if (ModemStat & MS_DSR_ON) {
        retval |= TIOCM_DSR;
    }
    if (ModemStat & MS_RING_ON) {
        retval |= TIOCM_RI;
    }
    return retval;
}

bool serial_set_baud(HANDLE comfid, DWORD baudrate) {
    DCB dcb;
    if (getcomm(comfid, &dcb)) {
        return true;
    }

    dcb.BaudRate = baudrate;

    return setcomm(comfid, &dcb);
}

int serial_timed_read_com(HANDLE handle, LPVOID buffer, DWORD len, DWORD ms) {
    HANDLE       hComm = (HANDLE)handle;
    COMMTIMEOUTS timeouts;
    DWORD        actual;
    BOOL         ret;

    timeouts.ReadIntervalTimeout         = 1;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.ReadTotalTimeoutConstant    = ms;
    timeouts.WriteTotalTimeoutMultiplier = 1;   // was 2
    timeouts.WriteTotalTimeoutConstant   = 10;  // was 100

    if (!SetCommTimeouts(hComm, &timeouts)) {
        printf("Can't set COM timeout\n");
        CloseHandle((HANDLE)hComm);
        return -1;
        // Error setting time-outs.
    }

    ret = ReadFile(hComm, (LPVOID)buffer, (DWORD)len, &actual, NULL);
    return actual;
}

int serial_write(HANDLE handle, LPCVOID buffer, DWORD len) {
    DWORD actual;
    (void)WriteFile((HANDLE)handle, (LPCVOID)buffer, (DWORD)len, (LPDWORD)&actual, NULL);
    return actual;
}

HANDLE serial_open_com(char* portname) {  // Open COM port
    wchar_t      wcomname[10];
    DCB          dcb;
    HANDLE       hComm;
    COMMTIMEOUTS timeouts;

    // swprintf() is a pain because it comes in two versions,
    // with and without the length parameter.  snwprintf() works
    // in all environments and is safer anyway.
    snwprintf(wcomname, 10, L"\\\\.\\%S", portname);
    hComm = CreateFileW(wcomname, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hComm == INVALID_HANDLE_VALUE) {
        return hComm;
    }

    FillMemory(&dcb, sizeof(dcb), 0);
    dcb.DCBlength = sizeof(DCB);

#ifdef NOTDEF
    if (!GetCommState(hComm, &dcb)) {
        printf("Can't get COM mode, error %d\n", GetLastError());
        CloseHandle((HANDLE)hComm);
        return INVALID_HANDLE_VALUE;
    }
    printf("\nBaudRate = %d, ByteSize = %d, Parity = %d, StopBits = %d\n", dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits);
#endif

    if (!BuildCommDCB("115200,n,8,1", &dcb)) {
        printf("Can't build DCB\n");
        CloseHandle((HANDLE)hComm);
        return INVALID_HANDLE_VALUE;
    }

    if (!SetCommState(hComm, &dcb)) {
        printf("Can't set COM mode, error %d\n", GetLastError());
        CloseHandle((HANDLE)hComm);
        return INVALID_HANDLE_VALUE;
    }

    timeouts.ReadIntervalTimeout         = 2;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.ReadTotalTimeoutConstant    = 100;
    timeouts.WriteTotalTimeoutMultiplier = 2;
    timeouts.WriteTotalTimeoutConstant   = 100;

    if (!SetCommTimeouts(hComm, &timeouts)) {
        printf("Can't set COM timeout\n");
        CloseHandle((HANDLE)hComm);
        return INVALID_HANDLE_VALUE;
        // Error setting time-outs.
    }

    return hComm;
}

extern char* comname;

HANDLE hFNC;

nvs_handle_t hw_nvs;

void init_system() {
    lgfx::Panel_sdl::setup();

    hw_nvs = nvs_init("hardware");

    scene_area = &xscene_area0;

    display.init();

    touch.begin(&display);
    hFNC = serial_open_com(comname);
    if (hFNC == INVALID_HANDLE_VALUE) {
        dbg_printf("Can't open %s\n", comname);
        exit(1);

    } else {
        serial_set_baud(hFNC, 115200);
    }

    // Draw the logo screen
    display.clear();
}

void drawButton(int n) {
#if 0
    Area& button = button_areas[n];
    int   w      = button.w() - 2 * button_inset;
    int   h      = button.h() - 2 * button_inset;
    display.fillRoundRect(button.x() + button_inset, button.y() + button_inset, w, h, button_inset, button_colors[n]);
    printf("Button %d x %d y %d\n", n, button.x(), button.y());
#endif
}

void base_display() {
    display.clear();
    // On-screen buttons
    for (int i = 0; i < n_buttons; i++) {
        drawButton(i);
    }
}

void next_layout(int n) {}
void resetFlowControl() {}

extern "C" void fnc_putchar(uint8_t c) {
    serial_write(hFNC, &c, 1);
}

extern "C" int fnc_getchar() {
    char c;
    int  cnt = serial_timed_read_com(hFNC, &c, 1, 1);
    if (cnt > 0) {
        update_rx_time();
#ifdef ECHO_FNC_TO_DEBUG
        dbg_write(c);
#endif
        return c;
    }
    return -1;
}

extern "C" void poll_extra() {}

void dbg_write(uint8_t c) {
    putchar(c);
}

void dbg_print(const char* s) {
    char c;
    while ((c = *s++) != '\0') {
        putchar(c);
    }
}

static bool outside_of_circle(int& x, int& y) {
    x -= display.width() / 2;
    y -= display.height() / 2;
    int magsq = x * x + y * y;
    return magsq > (120 * 120);
}

bool screen_encoder(int x, int y, int& delta) {
    return false;
}

bool switch_button_touched(bool& pressed, int& button) {
    return false;
}

bool hit(int button_num, int x, int y) {
    Point local;
    return button_areas[button_num].is_inside(Point(x, y), local);
}

struct button_debounce_t {
    bool    debouncing;
    bool    skipped;
    int32_t timeout;
} debounce[n_buttons] = { { false, false, 0 } };

bool    touch_debounce = false;
int32_t touch_timeout  = 0;

extern VMenu vmenu;

bool auxiliary_touch(int x, int y) {
    return vmenu.is_touched(x, y);
}

bool screen_button_touched(bool pressed, int x, int y, int& button) {
    for (int i = 0; i < n_buttons; i++) {
        if (hit(i, x, y)) {
            button = i;
            if (!pressed) {
                touch_debounce = true;
                touch_timeout  = milliseconds() + 100;
            }
            printf("Screen button hit %d\n", i);
            return true;
        }
    }
    return false;
}

void update_events() {
    lgfx::Panel_sdl::loop();
    auto ms = lgfx::millis();
    if (touch.isEnabled()) {
        if (touch_debounce) {
            if ((ms - touch_timeout) < 0) {
                return;
            }
            touch_debounce = false;
        }
        touch.update(ms);
    }
}

void ackBeep() {}

void deep_sleep() {}

int16_t get_encoder() {
    return 0;
}

static FILE* prefFile(const char* handle, const char* pname, const char* mode) {
    static char fname[60];
    snprintf(fname, 60, "%s/%s", handle, pname);

    return fopen(fname, mode);
}

void nvs_get_str(nvs_handle_t handle, const char* name, char* value, size_t* len) {
    FILE* fd = prefFile(handle, name, "rb");
    if (fd) {
        *len = fread(value, 1, *len - 1, fd);
        fclose(fd);
    } else {
        *len = 0;
    }
    value[*len] = '\0';
}
void nvs_set_str(nvs_handle_t handle, const char* name, const char* value) {
    FILE* fd = prefFile(handle, name, "wb");
    if (fd) {
        fwrite(value, 1, strlen(value), fd);
        fclose(fd);
    }
}

void nvs_get_i32(nvs_handle_t handle, const char* name, int32_t* value) {
    char   strval[20];
    size_t len = 20;
    nvs_get_str(handle, name, strval, &len);
    if (*strval) {
        *value = atoi(strval);
    }
}
void nvs_set_i32(nvs_handle_t handle, const char* name, int32_t value) {
    char valstr[20];
    snprintf(valstr, 20, "%d", value);
    nvs_set_str(handle, name, valstr);
}

nvs_handle_t nvs_init(const char* name) {
    char dname[50];
    _mkdir("prefs");
    snprintf(dname, 50, "prefs/%s", name);
    _mkdir(dname);

    return strdup(dname);
}
