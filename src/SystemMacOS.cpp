// 2026 - Figamore
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// System interface routines for macOS (native SDL preview build)

#include "stdio.h"

#include "System.h"
#include "FluidNCModel.h"
#include "M5GFX.h"
#include "Drawing.h"
#include "NVS.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

LGFX_Device& display = M5.Display;
LGFX_Sprite  canvas(&M5.Display);

m5::Speaker_Class& speaker = M5.Speaker;
m5::Touch_Class&   touch   = M5.Touch;

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
    drawPngFile(&canvas, filename, x, y);
}
void drawPngFile(LGFX_Sprite* sprite, const char* filename, int x, int y) {
    std::string fn("data/");
    fn += filename;
    sprite->drawPngFile(fn.c_str(), x, -y, 0, 0, 0, 0, 1.0f, 1.0f, datum_t::middle_center);
}

static int serial_fd = -1;

static bool open_serial(const char* portname) {
    serial_fd = open(portname, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd < 0) {
        printf("Can't open %s\n", portname);
        return false;
    }

    struct termios tty;
    tcgetattr(serial_fd, &tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag  = IGNBRK | IGNPAR;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(serial_fd, TCSANOW, &tty);
    return true;
}

extern char* comname;

void init_system() {
    lgfx::Panel_sdl::setup();

    auto cfg = M5.config();
    M5.begin(cfg);

    if (comname != NULL) {
        if (!open_serial(comname)) {
            printf("Running without serial connection\n");
        }
    } else {
        printf("No serial port given — running in preview mode (no FluidNC connection)\n");
    }

    canvas.createSprite(display.width(), display.height());

    display.clear();
    speaker.setVolume(255);
}

Point sprite_offset { 0, 0 };

void show_logo() {}
void base_display() {
    display.clear();
}

void next_layout(int delta) {}

void resetFlowControl() {}

extern "C" void fnc_putchar(uint8_t c) {
    if (serial_fd >= 0) {
        write(serial_fd, &c, 1);
    }
}

extern "C" int fnc_getchar() {
    if (serial_fd < 0) {
        return -1;
    }
    char c;
    int  cnt = (int)read(serial_fd, &c, 1);
    if (cnt > 0) {
        update_rx_time();
        return (unsigned char)c;
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
        return false;
    }

    int tangent = y * 100 / x;
    if (tangent < 0) {
        tangent = -tangent;
    }
    delta = 4;
    if (tangent > 172) {
        delta = 1;
    } else if (tangent > 100) {
        delta = 2;
    } else if (tangent > 58) {
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
    if (M5.BtnA.wasPressed()) {
        button  = 0;
        pressed = true;
        return true;
    }
    if (M5.BtnA.wasReleased()) {
        button  = 0;
        pressed = false;
        return true;
    }
    if (M5.BtnB.wasPressed()) {
        button  = 1;
        pressed = true;
        return true;
    }
    if (M5.BtnB.wasReleased()) {
        button  = 1;
        pressed = false;
        return true;
    }
    if (M5.BtnC.wasPressed()) {
        button  = 2;
        pressed = true;
        return true;
    }
    if (M5.BtnC.wasReleased()) {
        button  = 2;
        pressed = false;
        return true;
    }
    return false;
}

void ackBeep() {
    speaker.tone(1800, 50);
}

void deep_sleep(int us) {}

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
    mkdir("prefs", 0755);
    snprintf(dname, 50, "prefs/%s", name);
    mkdir(dname, 0755);
    return strdup(dname);
}

bool ui_locked() {
    return false;
}
