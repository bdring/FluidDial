// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for Windows

// stdio.h must precede the include of M5Unified.h in System.h
// in order for image files to work correctly
#include "stdio.h"

#include "System.h"
#include "FluidNCModel.h"
#include "M5GFX.h"
#include "Drawing.h"
#include "NVS.h"

#include <windows.h>
#include <commctrl.h>
#include <direct.h>

LGFX_Device& display = M5.Display;
LGFX_Sprite  canvas(&M5.Display);

m5::Speaker_Class& speaker     = M5.Speaker;
m5::Touch_Class&   touch       = M5.Touch;
m5::Button_Class&  dialButton  = M5.BtnB;
m5::Button_Class&  greenButton = M5.BtnC;
m5::Button_Class&  redButton   = M5.BtnA;

bool round_display = true;

void system_background() {
    drawPngFile("PCBackground.png", 0, 0);
}

void update_events() {
    lgfx::Panel_sdl::loop();
    M5.update();
}

extern "C" int milliseconds() {
    return m5gfx::millis();
}

void delay_ms(uint32_t ms) {
    SDL_Delay(ms);
}

void drawPngFile(const char* filename, int x, int y) {
    std::string fn("data/");
    fn += filename;
    // When datum is middle_center, the origin is the center of the canvas and the
    // +Y direction is down.
    canvas.drawPngFile(fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
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

void init_system() {
    lgfx::Panel_sdl::setup();

    auto cfg = M5.config();
    M5.begin(cfg);

    hFNC = serial_open_com(comname);
    if (hFNC == INVALID_HANDLE_VALUE) {
        dbg_printf("Can't open %s\n", comname);
        exit(1);

    } else {
        serial_set_baud(hFNC, 115200);
    }

    // Make an offscreen canvas that can be copied to the screen all at once
    canvas.createSprite(display.width(), display.height());

    // Draw the logo screen
    display.clear();
    speaker.setVolume(255);
}

Point sprite_offset { 0, 0 };

void base_display() {
    display.clear();
}

void next_layout(int delta) {}

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
    if (!outside_of_circle(x, y)) {
        return false;
    }
    if (y >= 0) {
        // The encoder area is the top half of the screen so
        // if we are in the bottom half, return 0.
        return false;
    }

    int tangent = y * 100 / x;
    if (tangent < 0) {
        tangent = -tangent;
    }
    delta = 4;
    if (tangent > 172) {  // tan(60)*100
        delta = 1;
    } else if (tangent > 100) {  // tan(45)*100
        delta = 2;
    } else if (tangent > 58) {  // tan(30)*100
        delta = 3;
    }
    if (x < 0) {
        delta = -delta;
    }
    return true;
}

bool screen_button_touched(bool pressed, int x, int y, int& button) {
    if (!outside_of_circle(x, y)) {
        return false;
    }
    if (x <= -90) {
        button = 0;
    } else if (x >= 90) {
        button = 2;
    } else {
        button = 1;
    }
    return true;
}

bool switch_button_touched(bool& pressed, int& button) {
    if (redButton.wasPressed()) {
        button  = 0;
        pressed = true;
        return true;
    }
    if (redButton.wasReleased()) {
        button  = 0;
        pressed = false;
        return true;
    }
    if (dialButton.wasPressed()) {
        button  = 1;
        pressed = true;
        return true;
    }
    if (dialButton.wasReleased()) {
        button  = 1;
        pressed = false;
        return true;
    }
    if (greenButton.wasPressed()) {
        button  = 2;
        pressed = true;
        return true;
    }
    if (greenButton.wasReleased()) {
        button  = 2;
        pressed = false;
        return true;
    }
    return false;
}

void ackBeep() {
    speaker.tone(1800, 50);
}

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
