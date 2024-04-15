// Copyright (c) 2023 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for Windows

#ifdef WINDOWS
#    include "System.h"
#    include "FluidNCModel.h"

#    include "stdio.h"
#    include <windows.h>
#    include <commctrl.h>

M5GFX&             display = M5.Display;
M5Canvas           canvas(&M5.Display);
m5::Speaker_Class& speaker     = M5.Speaker;
m5::Touch_Class&   touch       = M5.Touch;
m5::Button_Class&  dialButton  = M5.BtnB;
m5::Button_Class&  greenButton = M5.BtnC;
m5::Button_Class&  redButton   = M5.BtnA;

//
void update_events() {
    lgfx::Panel_sdl::loop();
    M5.update();

    auto ms = m5gfx::millis();
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

#    define TIOCM_LE 0x001
#    define TIOCM_DTR 0x002
#    define TIOCM_RTS 0x004
#    define TIOCM_ST 0x008
#    define TIOCM_SR 0x010
#    define TIOCM_CTS 0x020
#    define TIOCM_CAR 0x040
#    define TIOCM_RNG 0x080
#    define TIOCM_DSR 0x100
#    define TIOCM_CD TIOCM_CAR
#    define TIOCM_RI TIOCM_RNG

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

HANDLE serial_open_com(int portnum) {  // Open COM port
    wchar_t      wcomname[10];
    DCB          dcb;
    HANDLE       hComm;
    COMMTIMEOUTS timeouts;

    // swprintf() is a pain because it comes in two versions,
    // with and without the length parameter.  snwprintf() works
    // in all environments and is safer anyway.
    snwprintf(wcomname, 10, L"\\\\.\\COM%d", portnum);
    hComm = CreateFileW(wcomname, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hComm == INVALID_HANDLE_VALUE) {
        return hComm;
    }

    FillMemory(&dcb, sizeof(dcb), 0);
    dcb.DCBlength = sizeof(DCB);

#    ifdef NOTDEF
    if (!GetCommState(hComm, &dcb)) {
        printf("Can't get COM mode, error %d\n", GetLastError());
        CloseHandle((HANDLE)hComm);
        return INVALID_HANDLE_VALUE;
    }
    printf("\nBaudRate = %d, ByteSize = %d, Parity = %d, StopBits = %d\n", dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits);
#    endif

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

#    define FNC_COM_NUM 13

HANDLE hFNC;
void   init_system() {
    lgfx::Panel_sdl::setup();

    auto cfg = M5.config();
    M5.begin(cfg);

    hFNC = serial_open_com(FNC_COM_NUM);
    if (hFNC == INVALID_HANDLE_VALUE) {
        dbg_printf("Can't open COM%d\n", FNC_COM_NUM);
        exit(1);

    } else {
        serial_set_baud(hFNC, 115200);
    }

    //    display.setResolution(320, 320);
    // display.init();

    init_encoder();  // Use our own encoder driver
    // Make an offscreen canvas that can be copied to the screen all at once
    canvas.createSprite(display.width(), display.height());

    // Draw the logo screen
    display.clear();
    //    display.drawPngFile(LittleFS, "/fluid_dial.png", 0, 0, display.width(), display.height(), 0, 0, 0.0f, 0.0f, datum_t::middle_center);

    speaker.setVolume(255);
}

extern "C" void fnc_putchar(uint8_t c) {
    serial_write(hFNC, &c, 1);
}

static char    buf[128];
static int     bufcnt = 0;
extern "C" int fnc_getchar() {
    if (!bufcnt) {
        bufcnt = serial_timed_read_com(hFNC, buf, 128, 1);
    }
    if (bufcnt > 0) {
        --bufcnt;
        update_rx_time();
        char c = buf[bufcnt];
#    ifdef ECHO_FNC_TO_DEBUG
        dbg_write(c);
#    endif
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
#endif
