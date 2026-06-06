// 2026 - Figamore
//
//
// Controls:
//   Q / W / E  — red / dial / green button press & release
//   Scroll wheel inside the sprite area — rotary encoder
//   Mouse click / drag inside sprite — touch events

#include "stdio.h"

#include "System.h"
#include "Scene.h"
#include "FluidNCModel.h"
#include "Drawing.h"
#include "NVS.h"
#ifdef USE_WIFI
#    include "WiFiConnection.h"
#    include "PeerLink.h"
#endif

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>
#include <SDL.h>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

namespace lgfx {
    inline namespace v1 {
        class LGFX_CYD : public LGFX_Device {
            lgfx::Panel_sdl _panel;

            bool init_impl(bool, bool use_clear) override {
                return LGFX_Device::init_impl(false, use_clear);
            }

        public:
            LGFX_CYD() {
                auto cfg          = _panel.config();
                cfg.memory_width  = 240;
                cfg.panel_width   = 240;
                cfg.memory_height = 320;
                cfg.panel_height  = 320;
                _panel.config(cfg);
                _panel.setScaling(2, 2);
                setPanel(&_panel);
            }
        };
    }
}

static lgfx::LGFX_CYD _display;
LGFX_Device&           display = _display;
LGFX_Sprite            canvas(&_display);

m5::Touch_Class  _touch;
m5::Touch_Class& touch = _touch;

bool round_display = false;

static constexpr int n_buttons = 3;
static constexpr int button_w  = 80;
static constexpr int button_h  = 80;
static constexpr int sprite_wh = 240;
static constexpr int btn_y0    = sprite_wh;  // 240

static const int button_colors[n_buttons] = { RED, YELLOW, GREEN };

int     num_layouts = 1;
int32_t layout_num  = 0;
Point   sprite_offset { 0, 0 };

void set_layout(int /*n*/) {
    sprite_offset = { 0, 0 };
}

void next_layout(int /*delta*/) {
    // No-op in sim
}

static volatile int16_t _encoder_value = 0;

static int sdl_scroll_watch(void*, SDL_Event* ev) {
    if (ev->type == SDL_MOUSEWHEEL) {
        _encoder_value = (int16_t)(_encoder_value + ev->wheel.y);
    }
    return 0;
}

static const SDL_Scancode button_keys[n_buttons] = {
    SDL_SCANCODE_Q,
    SDL_SCANCODE_W,
    SDL_SCANCODE_E,
};
static bool _btn_last[n_buttons] = { false, false, false };

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
    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8;
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
    display.init();

    SDL_AddEventWatch(sdl_scroll_watch, nullptr);

    touch.begin(&display);
    touch.setFlickThresh(10);

    if (comname != nullptr) {
        if (!open_serial(comname)) {
            printf("Running without serial connection\n");
        }
    } else {
        printf("No serial port given — running in CYD preview mode (no FluidNC connection)\n");
    }

    canvas.createSprite(sprite_wh, sprite_wh);
    display.clear();
}

void update_events() {
    lgfx::Panel_sdl::loop();
    touch.update((uint32_t)lgfx::millis());
#ifdef DEV_SIMULATED_CONNECT
    update_rx_time();
    static bool _sim_done = false;
    if (!_sim_done) {
        _sim_done = true;
        begin_status_report();
        show_state("Idle");
        pos_t fake_axes[3]   = { -200000, 40000, 330000 };
        pos_t fake_wco[3]    = { 0, 0, 0 };
        bool  fake_limits[3] = { false, false, false };
        show_dro(fake_axes, fake_wco, false, fake_limits, 3);
        end_status_report();
    }
#endif
}

extern "C" int milliseconds() {
    return (int)lgfx::millis();
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
    int  n = (int)read(serial_fd, &c, 1);
    if (n > 0) {
        update_rx_time();
        return (unsigned char)c;
    }
    return -1;
}

extern "C" void poll_extra() {}

extern "C" bool fnc_rx_waiting() { return false; }

void resetFlowControl() {}
void reinit_fnc_uart() {}

void dbg_write(uint8_t c) {
    putchar(c);
}

void dbg_print(const char* s) {
    char c;
    while ((c = *s++) != '\0') {
        putchar(c);
    }
}

void    ackBeep()             {}
void    deep_sleep(int /*us*/) {}
int16_t get_encoder()         { return _encoder_value; }

bool ui_locked(bool /*redrawButtonsFlag*/) {
    return false;
}

static const char* button_pngs[n_buttons] = {
    "data/red_button.png", "data/orange_button.png", "data/green_button.png"
};

void redrawButtons() {
    bool show = !current_scene || current_scene->showButtons();
    display.startWrite();
    for (int i = 0; i < n_buttons; i++) {
        int bx = i * button_w;
        int by = btn_y0;
        display.fillRect(bx, by, button_w, button_h, BLACK);
        if (show) {
            display.fillCircle(bx + button_w / 2, by + button_h / 2, 28, button_colors[i]);
            display.drawPngFile(button_pngs[i], bx + 10, by + 10, 60, 60);
        }
    }
    display.endWrite();
}

void show_logo() {
    display.clear();
}

void base_display() {
    redrawButtons();
}

void system_background() {
    drawBackground(BLACK);
}

bool screen_button_touched(bool /*pressed*/, int x, int y, int& button) {
    if (y < btn_y0 || y >= btn_y0 + button_h) {
        return false;
    }
    int idx = x / button_w;
    if (idx < 0 || idx >= n_buttons) {
        return false;
    }
    button = idx;
    return true;
}

bool switch_button_touched(bool& pressed, int& button) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < n_buttons; i++) {
        bool cur = (keys[button_keys[i]] != 0);
        if (cur != _btn_last[i]) {
            _btn_last[i] = cur;
            button       = i;
            pressed      = cur;
            return true;
        }
    }
    return false;
}

bool screen_encoder(int /*x*/, int /*y*/, int& /*delta*/) {
    return false;
}

static FILE* prefFile(const char* handle, const char* pname, const char* mode) {
    static char fname[60];
    snprintf(fname, sizeof(fname), "%s/%s", handle, pname);
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
    char   s[20];
    size_t len = sizeof(s);
    nvs_get_str(handle, name, s, &len);
    if (*s) {
        *value = atoi(s);
    }
}

void nvs_set_i32(nvs_handle_t handle, const char* name, int32_t value) {
    char s[20];
    snprintf(s, sizeof(s), "%d", value);
    nvs_set_str(handle, name, s);
}

nvs_handle_t nvs_init(const char* name) {
    char dname[50];
    mkdir("prefs", 0755);
    snprintf(dname, sizeof(dname), "prefs/%s", name);
    mkdir(dname, 0755);
    return strdup(dname);
}

int  battery_level()           { return 75; }
bool battery_charging()        { return true; }
int  adc_millivolts(int pin)   { return (pin == 39) ? 2484 : 0; }
int  battery_adc_millivolts()  { return adc_millivolts(39); }
int  battery_millivolts()      { return 3810; }

#ifdef USE_WIFI
static WiFiConfig _preview_cfg = { "FluidNC", "", "192.168.0.1", true };

void        wifi_init(bool)                              {}
void        wifi_poll()                                  {}
bool        wifi_is_connected()                          { return true; }
bool        websocket_is_connected()                     { return true; }
void        wifi_force_ws_reconnect()                    {}
void        wifi_shutdown()                              {}
bool        wifi_in_ap_mode()                            {
#ifdef DEV_SIMULATED_AP_MODE
    return true;
#else
    return false;
#endif
}
void        wifi_start_ap_setup()                        {}
void        wifi_stop_ap_and_restart()                   {}
void        wifi_stop_ap()                               {}
void        wifi_save_config(const char*, const char*, const char*) {}
WiFiConfig  wifi_load_config()                           { return _preview_cfg; }
const char* wifi_ap_ssid()                               { return "FluidDial"; }
const char* wifi_status_str()                            { return "Connected"; }
const bool  wifi_not_ready()                             { return false; }
int         wifi_signal_bars()                           { return 3; }
const char* wifi_last_error()                            { return nullptr; }
WiFiConfig  wifi_active_config()                         { return _preview_cfg; }
void        ws_putchar(uint8_t)                          {}
int         ws_getchar()                                 { return -1; }
bool          wifi_use_uart_mode()                           { return false; }
void          wifi_set_uart_mode(bool)                       {}
bool          wifi_is_first_boot()                           { return false; }
TransportMode wifi_get_transport()                           { return TransportMode::WIFI; }
void          wifi_set_transport(TransportMode)              {}
bool          wifi_use_espnow_mode()                         { return false; }
void          wifi_request_ota_reboot()                      {}
bool          wifi_ota_boot_requested()                      { return false; }

// ---- OTA stubs -----
// Default to the STA "ready" view. Define DEV_SIMULATED_OTA_AP to preview the
// AP-credentials view instead
void        wifi_start_ota_server()    {}
void        wifi_stop_ota_server()     {}
void        wifi_ota_force_ap_setup()  {}
int         wifi_ota_progress()        { return 0; }
const char* wifi_ota_ip()              { return "192.168.1.100"; }
const char* wifi_ota_error()           { return nullptr; }
#ifdef DEV_SIMULATED_OTA_AP
bool        wifi_ota_ap_mode()         { return true; }
bool        wifi_ota_sta_connected()   { return false; }
#else
bool        wifi_ota_ap_mode()         { return false; }
bool        wifi_ota_sta_connected()   { return true; }
#endif

// ESP-NOW stubs
void        espnow_init()                                {}
void        espnow_poll()                                {}
void        espnow_putchar(uint8_t)                      {}
int         espnow_getchar()                             { return -1; }
bool        espnow_is_paired()                           { return true; }
bool        espnow_is_connected()                        { return true; }
const char* espnow_status_str()                          { return "Simulated"; }
void        espnow_start_pairing()                       {}
void        espnow_cancel_pairing()                      {}
bool        espnow_pairing_complete()                    { return false; }
void        espnow_clear_pairing()                       {}
bool        espnow_has_saved_pairing()                   { return true; }
bool        espnow_is_reconnecting()                     { return false; }
int8_t      espnow_rssi()                                { return 0; }
int         espnow_signal_bars()                         { return 3; }
size_t      espnow_profile_count()                       { return 2; }
int         espnow_active_profile_index()                { return 0; }
bool        espnow_get_profile(size_t index, ESPNowProfileInfo& out) {
    memset(&out, 0, sizeof(out));
    if (index >= 2) return false;
    static const uint8_t macs[2][6] = {
        {0x44, 0x1d, 0x64, 0xf2, 0x27, 0xe4},
        {0x24, 0x6f, 0x28, 0xaa, 0xbb, 0xcc},
    };
    memcpy(out.mac, macs[index], sizeof(out.mac));
    out.channel = index == 0 ? 1 : 6;
    out.active = index == 0;
    strlcpy(out.hostname, index == 0 ? "fluidnc" : "router-cnc", sizeof(out.hostname));
    return true;
}
bool        espnow_select_profile(size_t)                { return true; }
#endif  // USE_WIFI
