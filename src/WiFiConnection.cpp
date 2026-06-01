// 2026 - Figamore (original WebSocket scaffolding)
// 2026 - Paul Mokbel (Telnet transport)
// Use of this source code is governed by a GPLv3 license.
//
// WiFi transport layer for FluidDial.
//
// Provides fnc_putchar() and fnc_getchar() over a raw TCP (Telnet) socket
// to FluidNC on port 23. The public API still carries `ws_*` and `*_ws_*`
// names because the surrounding scenes were already wired against them;
// the implementation underneath is lwip sockets, not WebSocket.

#ifdef ARDUINO

#include "WiFiConnection.h"
#include "FluidNCModel.h"
#include "FileParser.h"  // json_reset_depth()
#include "System.h"
#include "Scene.h"   // request_redisplay()

#include <Esp.h>
#include <esp_attr.h>     // RTC_NOINIT_ATTR - survives soft reboot, cleared on power loss
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Update.h>
#include <atomic>

extern const char* git_info;

#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mdns.h>          // mdns_query_a() — ESP-IDF multicast DNS
#include <fcntl.h>
#include <errno.h>

// ─── Configuration ───────────────────────────────────────────────────────────

#define WIFI_AP_SSID       "FluidDial"
#define WIFI_AP_PASS       ""          // Open AP — no password needed
#define PREF_NAMESPACE     "fluidwifi"
#define FLUIDNC_TELNET_PORT 23
#define HARDCODE_TEST_WIFI 0
#define TEST_WIFI_SSID     "FluidNC"
#define TEST_WIFI_PASS     "12345678"
#define TEST_FLUIDNC_IP    "192.168.0.1"
#define RX_BUF_SIZE        2048
#define TX_BUF_SIZE        512
#define STATUS_POLL_MS     500         // Send '?' every 500 ms while connected
#define WIFI_RETRY_DELAY_MS 15000     // Retry WiFi.begin() this long after a failure
#define DNS_RETRY_DELAY_MS  5000     // Retry hostname resolution this long after a failure
#define TCP_RECONNECT_DELAY_MS 2000   // Retry TCP connect this long after a failure
#define TCP_CONNECT_TIMEOUT_MS 4000   // Give up an in-progress (non-blocking) connect after this long
#define WIFI_RX_STALL_MS       3000   // Drop a connected-but-silent socket after this long (wedged half-open)

// ─── Globals ─────────────────────────────────────────────────────────────────

static int              _sock = -1;
static uint32_t         _tcp_next_try_ms = 0;
static int              _connecting_fd = -1;          // fd of an in-progress non-blocking connect, else -1
static uint32_t         _tcp_connect_deadline_ms = 0; // abandon the in-progress connect at this time
static char             _fluidnc_remote_ip[40] = {};  // dotted-decimal, cached for reconnects
static WebServer        httpServer(80);
static DNSServer        dnsServer;

static bool _ota_server_active   = false;
static bool _ota_ap_mode         = false;
static bool _ota_started_ap      = false;
static bool _ota_started_sta     = false;
static volatile int _ota_progress = 0;
static char _ota_ip_str[40]      = {};
static wl_status_t _ota_last_sta = WL_IDLE_STATUS;
static const char* _ota_error_msg     = nullptr;
static uint32_t    _ota_connect_start_ms = 0;

static bool _ap_mode            = false;
static bool _ws_connected       = false;
static bool _ws_started         = false;
static bool _wifi_was_connected = false;
static bool _wifi_stack_started = false;  // true only after wifi_init() actually ran
static WiFiConfig _active_cfg = {};
static wl_status_t      _last_wifi_status        = WL_IDLE_STATUS;
static const char*      _wifi_error_msg           = nullptr;  // human-readable failure, cleared on success
static uint32_t         _wifi_retry_at            = 0;        // millis() target for the next WiFi.begin() retry
static volatile uint8_t _wifi_disconnect_reason   = 0;        // set by WiFi event, read in wifi_poll()
static bool             _wifi_ever_connected      = false;    // true once WL_CONNECTED seen; blocks false-positive on drops
static uint32_t         _wifi_connect_start_ms    = 0;        // millis() when WiFi.begin() was last issued
static uint8_t          _handshake_timeout_count  = 0;        // consecutive 4-way handshake timeouts; real auth fail after threshold

// Ring buffer for characters received from FluidNC over Telnet.
// Refilled by tcp_refill_rx() from wifi_poll(); read by fnc_getchar().
static uint8_t _rx_buf[RX_BUF_SIZE];
static int     _rx_head = 0;
static int     _rx_tail = 0;

// Line buffer for outgoing text (GCode / $ commands).
static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;

// Timers.
static uint32_t _last_status_ms = 0;
static uint32_t _last_rx_ms     = 0;   // millis() of the last byte received; drives the RX-stall watchdog

// ─── Async hostname resolution ────────────────────────────────────────────────
static volatile bool _dns_resolving    = false;  // background task is in flight
static volatile bool _dns_done         = false;  // task finished; read on main task
static volatile bool _dns_ok           = false;  // resolution succeeded
static char          _dns_result_str[40] = {};   // resolved IP string (written by task)
static uint32_t      _dns_retry_at       = 0;    // millis() target for next DNS retry (0 = no retry pending)

static bool is_dotted_decimal(const char* s) {
    for (const char* p = s; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || *p == '.')) return false;
    }
    return true;
}

// Resolve a hostname to an IPv4 address using the correct path for the
// name shape:
//   *.local  → ESP-IDF mdns_query_a (multicast).
//   anything else → unicast DNS via lwip / WiFi.hostByName.
// WiFi.hostByName() tries unicast first then mDNS, which is the wrong
// order for .local — unicast may resolve via a misconfigured upstream
// resolver (or a router that hijacks .local), yielding a confidently
// wrong address. Going straight to mdns_query_a avoids that.
static bool resolve_host_strict(const char* host, IPAddress& out) {
    size_t len = strlen(host);
    const char* dot_local = ".local";
    size_t dl_len = strlen(dot_local);
    if (len > dl_len && strcasecmp(host + len - dl_len, dot_local) == 0) {
        // Strip the .local suffix; mdns_query_a appends it internally.
        char base[64];
        size_t base_len = len - dl_len;
        if (base_len >= sizeof(base)) {
            return false;
        }
        memcpy(base, host, base_len);
        base[base_len] = '\0';
        esp_ip4_addr_t addr = {};
        esp_err_t      err  = mdns_query_a(base, 2000, &addr);
        if (err != ESP_OK) {
            dbg_printf("mDNS: %s.local err=%d\n", base, (int)err);
            return false;
        }
        out = IPAddress(addr.addr);
        return true;
    }
    return WiFi.hostByName(host, out) == 1;
}

static void dnsResolveTask(void* /*param*/) {
    IPAddress ip;
    // resolve_host_strict() blocks on mDNS or unicast DNS depending on the
    // hostname shape — running it on a background task keeps the main loop
    // responsive while the multicast query waits up to 2 s for a reply.
    bool ok = resolve_host_strict(_active_cfg.fluidnc_ip, ip);
    if (ok) {
        ip.toString().toCharArray(_dns_result_str, sizeof(_dns_result_str));
    }
    _dns_ok        = ok;
    _dns_done      = true;   // written last — signals main task after result is ready
    _dns_resolving = false;
    vTaskDelete(nullptr);
}

static void start_dns_resolve() {
    _dns_done      = false;
    _dns_ok        = false;
    _dns_resolving = true;
    dbg_printf("Resolving hostname (async): %s\n", _active_cfg.fluidnc_ip);
    BaseType_t task_created = xTaskCreate(dnsResolveTask, "dns_resolve", 8192, nullptr, 1, nullptr);
    if (task_created != pdPASS) {
        _dns_resolving = false;
        _wifi_error_msg = "DNS resolve task start failed";
        dbg_printf("Failed to start DNS resolve task\n");
    }
}

// Ring buffer push/pop are defined further down; forward-declare so
// tcp_refill_rx() can push received bytes from up here.
static inline void rx_push(uint8_t c);

static void tcp_close() {
    if (_sock >= 0) {
        ::close(_sock);
        _sock = -1;
    }
    if (_connecting_fd >= 0) {
        ::close(_connecting_fd);
        _connecting_fd = -1;
    }
    _tcp_connect_deadline_ms = 0;
    _ws_connected = false;
    // Any JSON document still being streamed across chunks is now lost;
    // reset the depth tracker so the next document starts cleanly.
    json_reset_depth();
}

static void tcp_apply_opts(int fd) {
    // TCP_NODELAY: send realtime ('?', '!', '~', XON, etc.) as their own
    // segments instead of waiting for Nagle to coalesce.
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Bound how long a blocking send() will wait if FluidNC's RX is full.
    struct timeval snd_tv = { 0, 50000 };  // 50 ms
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv));

    // Detect dead peers faster than the default ~2 hour kernel timeout.
    int keepalive  = 1;
    int idle_s     = 10;
    int interval_s = 3;
    int count      = 3;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle_s, sizeof(idle_s));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval_s, sizeof(interval_s));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}

static void tcp_promote_connected(int fd, const char* host) {
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl != -1) {
        ::fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    }
    tcp_apply_opts(fd);
    _sock                    = fd;
    _connecting_fd           = -1;
    _tcp_connect_deadline_ms = 0;
    _ws_connected            = true;
    _last_status_ms          = 0;
    _last_rx_ms              = millis();   // grace window before the RX-stall watchdog can fire
    dbg_printf("Telnet: connected to %s:%d (fd=%d)\n", host, FLUIDNC_TELNET_PORT, fd);
    request_redisplay();
}

// Begin a non-blocking connect to FluidNC's telnet server
static bool tcp_open(const char* host) {
    dbg_printf("Starting Telnet: %s:%d\n", host, FLUIDNC_TELNET_PORT);
    IPAddress ip;
    if (!ip.fromString(host)) {
        dbg_printf("Telnet: bad IP literal: %s\n", host);
        return false;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        dbg_printf("Telnet: socket errno=%d\n", errno);
        return false;
    }
    int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl == -1 || ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1) {
        dbg_printf("Telnet: fcntl errno=%d\n", errno);
        ::close(fd);
        return false;
    }
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(FLUIDNC_TELNET_PORT);
    dest.sin_addr.s_addr = (uint32_t)ip;
    int rc = ::connect(fd, (struct sockaddr*)&dest, sizeof(dest));
    if (rc == 0) {
        tcp_promote_connected(fd, host);
        return true;
    }
    if (errno != EINPROGRESS) {
        // Fast failure (e.g. EHOSTUNREACH=113) — no SYN timeout to wait on.
        dbg_printf("Telnet: connect errno=%d\n", errno);
        ::close(fd);
        return false;
    }
    // Connect is in flight; tcp_poll_connect() will finish it.
    _connecting_fd           = fd;
    _tcp_connect_deadline_ms = millis() + TCP_CONNECT_TIMEOUT_MS;
    return true;
}

// Drive an in-progress non-blocking connect.
static void tcp_poll_connect() {
    if (_connecting_fd < 0) {
        return;
    }
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(_connecting_fd, &wfds);
    struct timeval tv = { 0, 0 };  // poll, don't block
    int sel = ::select(_connecting_fd + 1, nullptr, &wfds, nullptr, &tv);
    if (sel > 0 && FD_ISSET(_connecting_fd, &wfds)) {
        int       so_error = 0;
        socklen_t len      = sizeof(so_error);
        ::getsockopt(_connecting_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error == 0) {
            tcp_promote_connected(_connecting_fd, _fluidnc_remote_ip);
            return;
        }
        dbg_printf("Telnet: connect failed errno=%d\n", so_error);
        ::close(_connecting_fd);
        _connecting_fd           = -1;
        _tcp_connect_deadline_ms = 0;
        _tcp_next_try_ms         = millis() + TCP_RECONNECT_DELAY_MS;
        return;
    }
    if ((int32_t)(millis() - _tcp_connect_deadline_ms) >= 0) {
        dbg_println("Telnet: connect timed out");
        ::close(_connecting_fd);
        _connecting_fd           = -1;
        _tcp_connect_deadline_ms = 0;
        _tcp_next_try_ms         = millis() + TCP_RECONNECT_DELAY_MS;
    }
}

static void tcp_begin(const char* host) {
    // `host` is the resolved dotted-decimal IP (either from is_dotted_decimal
    // shortcut or from start_dns_resolve()). Cache it so reconnects after a
    // transient drop don't have to redo DNS for a still-valid host.
    strncpy(_fluidnc_remote_ip, host, sizeof(_fluidnc_remote_ip) - 1);
    _fluidnc_remote_ip[sizeof(_fluidnc_remote_ip) - 1] = '\0';
    _ws_started = true;
    if (!tcp_open(host)) {
        _tcp_next_try_ms = millis() + TCP_RECONNECT_DELAY_MS;
    }
}

static const char* wifi_status_name(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD:
            return "WL_NO_SHIELD";
        case WL_IDLE_STATUS:
            return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL:
            return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:
            return "WL_SCAN_COMPLETED";
        case WL_CONNECTED:
            return "WL_CONNECTED";
        case WL_CONNECT_FAILED:
            return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "WL_DISCONNECTED";
        default:
            return "WL_UNKNOWN";
    }
}

// Bulk send over the raw socket. `flags` is passed to send(); pass MSG_DONTWAIT
// for the gcode/jog stream so a backpressured socket never blocks the main loop
// (see ws_send_text). transient EAGAIN drops the remainder of this flush but
// keeps the socket so the in-flight document survives. Any other errno tears the
// socket down so wifi_poll() can reconnect.
static bool tcp_send_all(const uint8_t* payload, size_t length, int flags) {
    if (_sock < 0 || length == 0) {
        return _sock >= 0;
    }
    size_t off = 0;
    while (off < length) {
        ssize_t n = ::send(_sock, payload + off, length - off, flags);
        if (n <= 0) {
            int e = errno;
            if (e == EAGAIN || e == EWOULDBLOCK) {
                dbg_printf("Telnet: send EAGAIN dropped=%u of %u\n",
                           (unsigned)(length - off), (unsigned)length);
                return true;
            }
            dbg_printf("Telnet: send errno=%d off=%u/%u\n",
                       e, (unsigned)off, (unsigned)length);
            tcp_close();
            set_disconnected_state();
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

// Keep the ws_send_text/ws_send_bin spellings for the ws_putchar
// callsites — both go through the same raw send() now.
static bool ws_send_text(uint8_t* payload, size_t length) {
    return tcp_send_all(payload, length, MSG_DONTWAIT);
}

// Realtime bytes ('?', '!', '~', JogCancel, …): kept blocking
static bool ws_send_bin(const uint8_t* payload, size_t length) {
    return tcp_send_all(payload, length, 0);
}

// Pull bytes off the socket and push them into the RX ring buffer. Called
// from wifi_poll() — non-blocking. Bounded by available ring space so a
// burst from FluidNC (preferences.json is ~5 KB and arrives in one shot)
// can't overflow the ring and silently drop bytes mid-document, which
// corrupts the streaming JSON parser. Any data we don't read stays in
// the kernel's TCP receive window; TCP flow control will pause FluidNC
// if we fall too far behind.
static void tcp_refill_rx() {
    if (_sock < 0) {
        return;
    }
    uint8_t buf[256];
    while (true) {
        // Compute headroom in the ring buffer (leave one slot empty to
        // distinguish full from empty). If we'd overrun, stop and let
        // fnc_poll drain some bytes first.
        int used = (_rx_head - _rx_tail + RX_BUF_SIZE) % RX_BUF_SIZE;
        int free = RX_BUF_SIZE - used - 1;
        if (free <= 0) {
            return;
        }
        size_t want = (size_t)free < sizeof(buf) ? (size_t)free : sizeof(buf);
        ssize_t n = ::recv(_sock, buf, want, MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0) {
                dbg_println("Telnet: peer closed");
                tcp_close();
                set_disconnected_state();
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                dbg_printf("Telnet: recv errno=%d\n", errno);
                tcp_close();
                set_disconnected_state();
            }
            return;
        }
        for (ssize_t i = 0; i < n; i++) {
            uint8_t c = buf[i];
            if (c == '\r') continue;  // strip CR; GrblParserC wants LF-terminated lines
            rx_push(c);
        }
        update_rx_time();
        _last_rx_ms = millis();
        if ((size_t)n < want) {
            // Kernel had less than we asked for; nothing more for now.
            return;
        }
    }
}

// ─── Ring-buffer helpers ──────────────────────────────────────────────────────

static inline void rx_push(uint8_t c) {
    int next = (_rx_head + 1) % RX_BUF_SIZE;
    if (next != _rx_tail) {         // Drop on overflow
        _rx_buf[_rx_head] = c;
        _rx_head          = next;
    }
}

static inline int rx_pop() {
    if (_rx_head == _rx_tail) return -1;
    uint8_t c = _rx_buf[_rx_tail];
    _rx_tail  = (_rx_tail + 1) % RX_BUF_SIZE;
    return (unsigned char)c;
}

// ─── Telnet transport primitives ─────────────────────────────────────────────
// These are called by fnc_putchar/fnc_getchar (defined in SystemArduino.cpp)
// when the active transport is WiFi. Kept under the ws_* spelling so the
// SystemArduino dispatch and simulator stubs don't have to rename.

// Send one byte to FluidNC over the Telnet socket.
void ws_putchar(uint8_t c) {
    // Skip UART XON/XOFF flow-control bytes (irrelevant over TCP).
    if (c == 0x11 || c == 0x13) return;

    // Extended single-byte realtime commands: Ctrl-X, 0x80–0x9F, and the
    // IO-extender commands 0xB0–0xB3 (ACK=0xB2, NAK=0xB3). Send each one
    // immediately so it doesn't sit behind buffered g-code text.
    if (c == 0x18 || (c >= 0x80 && c <= 0x9F) || (c >= 0xB0 && c <= 0xB3)) {
        if (_ws_connected) ws_send_bin(&c, 1);
        return;
    }

    // ASCII realtime commands ('?', '!', '~') sent *outside* a text line.
    if (_tx_len == 0 && (c == '?' || c == '!' || c == '~')) {
        if (_ws_connected) ws_send_bin(&c, 1);
        return;
    }

    // Everything else: buffer until '\n', then send as a text frame.
    _tx_buf[_tx_len++] = c;
    if (c == '\n' || _tx_len >= TX_BUF_SIZE - 1) {
        if (_ws_connected) {
            ws_send_text(_tx_buf, _tx_len);
        }
        _tx_len = 0;
    }
}

// Receive one byte from the RX ring buffer (-1 if empty).
int ws_getchar() {
    return rx_pop();
}

bool ws_rx_available() {
    return _rx_head != _rx_tail;
}

// ─── Runtime transport mode ───────────────────────────────────────────────────

static int _transport_cached = -1;  // -1 = not yet loaded from NVS

TransportMode wifi_get_transport() {
    if (_transport_cached >= 0) return (TransportMode)_transport_cached;

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);

    if (prefs.isKey("transport")) {
        _transport_cached = (int)prefs.getUChar("transport", (uint8_t)TransportMode::WIFI);
    } else {
        bool uart = prefs.getBool("uart_mode", false);
        _transport_cached = uart ? (int)TransportMode::UART : (int)TransportMode::WIFI;
    }
    prefs.end();
#ifndef USE_ESPNOW
    // ESP-NOW excluded from this build: if a saved pairing selected it, fall back
    // to WiFi so nothing downstream tries to use the unavailable transport
    if (_transport_cached == (int)TransportMode::ESPNOW) {
        _transport_cached = (int)TransportMode::WIFI;
    }
#endif
    return (TransportMode)_transport_cached;
}

void wifi_set_transport(TransportMode mode) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putUChar("transport",  (uint8_t)mode);
    prefs.putBool("setup_done", true);  // clears first-boot flag
    prefs.end();
    _transport_cached = (int)mode;
    const char* names[] = {"UART", "WiFi", "ESP-NOW"};
    dbg_printf("Transport mode set to: %s\n", names[(int)mode < 3 ? (int)mode : 1]);
}

bool wifi_use_uart_mode()   { return wifi_get_transport() == TransportMode::UART; }
bool wifi_use_espnow_mode() { return wifi_get_transport() == TransportMode::ESPNOW; }

void wifi_set_uart_mode(bool uart) {
    wifi_set_transport(uart ? TransportMode::UART : TransportMode::WIFI);
}

bool wifi_is_first_boot() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    bool done = prefs.getBool("setup_done", false);
    prefs.end();
    return !done;
}

// --- OTA credentials HTML (served from AP when no WiFi is configured) ---

static const char OTA_CREDENTIALS_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial - WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#111;color:#eee;padding:20px;max-width:480px;margin:0 auto}
h1{color:#4CAF50;font-size:20px;margin-bottom:6px}
.sub{color:#888;font-size:13px;margin-bottom:20px;line-height:1.5}
label{display:block;margin:14px 0 5px;color:#ccc;font-size:14px}
input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #555;background:#2a2a2a;color:#eee;font-size:15px}
input:focus,select:focus{outline:none;border-color:#4CAF50}
.row{display:flex;gap:8px}
.row input{flex:1}
.scan-btn{padding:10px 14px;background:#2a2a2a;color:#4CAF50;border:1px solid #4CAF50;border-radius:6px;font-size:14px;cursor:pointer;white-space:nowrap;font-weight:bold}
.scan-btn:hover{background:#1e3d1e}
.scan-btn:disabled{color:#555;border-color:#555;cursor:default}
#netList{display:none;margin-top:6px}
.scan-status{font-size:13px;color:#aaa;margin-top:4px;min-height:18px}
.eye-btn{padding:10px 12px;background:#2a2a2a;color:#aaa;border:1px solid #555;border-radius:6px;font-size:18px;cursor:pointer;line-height:1}
.eye-btn:hover{color:#4CAF50;border-color:#4CAF50}
button[type=submit]{margin-top:22px;width:100%;padding:13px;background:#4CAF50;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer;font-weight:bold}
button[type=submit]:hover{background:#45a049}
.note{margin-top:16px;font-size:13px;color:#666;line-height:1.5;text-align:center}
.divider{margin:26px 0 6px;border-top:1px solid #333;text-align:center}
.divider span{position:relative;top:-11px;background:#111;padding:0 12px;color:#666;font-size:12px;text-transform:uppercase;letter-spacing:.5px}
.manual{font-size:13px;color:#888;margin-bottom:10px;line-height:1.5}
input[type=file]{padding:8px;font-size:13px}
.upbtn{margin-top:12px;width:100%;padding:12px;background:#2a2a2a;color:#4CAF50;border:1px solid #4CAF50;border-radius:6px;font-size:15px;cursor:pointer;font-weight:bold}
.upbtn:hover{background:#1e3d1e}.upbtn:disabled{color:#555;border-color:#555;cursor:default}
.bar-bg{display:none;background:#222;border-radius:4px;height:18px;margin:12px 0 4px;border:1px solid #444;overflow:hidden}
#ubar{height:100%;background:#4CAF50;width:0%;transition:width .2s}
#ustat{text-align:center;font-size:13px;color:#888;min-height:18px}
</style>
</head>
<body>
<h1>FluidDial WiFi Setup</h1>
<p class="sub">Please enter your WiFi credentials to fetch online updates. The device will restart and join your network.</p>
<form method="POST" action="/wifi-save">
  <label>Network Name (SSID)</label>
  <div class="row">
    <input type="text" name="ssid" id="ssid" placeholder="Your WiFi network" autocomplete="off" required>
    <button type="button" class="scan-btn" id="scanBtn" onclick="doScan()">Scan</button>
  </div>
  <select id="netList" onchange="pickNet(this)">
    <option value="">-- select a network --</option>
  </select>
  <div id="scanStatus" class="scan-status"></div>
  <label>Password</label>
  <div class="row">
    <input type="text" name="pass" id="pass" placeholder="Leave blank for open networks">
    <button type="button" class="eye-btn" id="eyeBtn" onclick="togglePass()">&#x1F441;</button>
  </div>
  <button type="submit">Save &amp; Connect</button>
</form>
<p class="note">After connecting, open <strong>fluiddial.local</strong> in your browser to access OTA updates.</p>

<div class="divider"><span>or</span></div>
<p class="manual">Already have a firmware <strong>.bin</strong> and don't want to connect to WiFi? Upload it directly (use the app-only <strong>firmware.bin</strong>, not the merged image).</p>
<form id="uf">
  <input type="file" id="ufile" accept=".bin" required>
  <button class="upbtn" type="submit" id="ubtn">Upload &amp; Flash</button>
</form>
<div class="bar-bg" id="ubarbg"><div id="ubar"></div></div>
<div id="ustat"></div>

<script>
function doScan(){
  var btn=document.getElementById('scanBtn');
  var st=document.getElementById('scanStatus');
  btn.disabled=true; btn.textContent='Scanning...';
  st.textContent='Scanning for networks, please wait...';
  document.getElementById('netList').style.display='none';
  pollScan();
}
function pollScan(){
  fetch('/scan').then(function(r){
    if(r.status===202){setTimeout(pollScan,1500);return null;}
    return r.json();
  }).then(function(nets){
    if(!nets)return;
    var lst=document.getElementById('netList');
    var st=document.getElementById('scanStatus');
    var btn=document.getElementById('scanBtn');
    lst.innerHTML='<option value="">-- select a network --</option>';
    nets.sort(function(a,b){return b.rssi-a.rssi;});
    nets.forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=n.ssid+(n.secure?' [secured]':'')+'  ('+n.rssi+' dBm)';
      lst.appendChild(o);
    });
    lst.style.display='block';
    st.textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found.';
    btn.disabled=false; btn.textContent='Scan';
  }).catch(function(){
    document.getElementById('scanStatus').textContent='Scan failed. Try again.';
    document.getElementById('scanBtn').disabled=false;
    document.getElementById('scanBtn').textContent='Scan';
  });
}
function pickNet(sel){
  if(sel.value) document.getElementById('ssid').value=sel.value;
}
function togglePass(){
  var p=document.getElementById('pass');
  var b=document.getElementById('eyeBtn');
  if(p.type==='text'){p.type='password';b.style.color='#555';}
  else{p.type='text';b.style.color='#aaa';}
}
document.getElementById('uf').onsubmit=function(e){
  e.preventDefault();
  var f=document.getElementById('ufile').files[0];
  if(!f)return;
  var bg=document.getElementById('ubarbg'),bar=document.getElementById('ubar');
  var st=document.getElementById('ustat'),btn=document.getElementById('ubtn');
  bg.style.display='block'; btn.disabled=true; st.textContent='Uploading '+f.name+'...';
  var fd=new FormData(); fd.append('firmware',f);
  var x=new XMLHttpRequest(); x.open('POST','/update');
  x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.round(ev.loaded/ev.total*100);bar.style.width=p+'%';st.textContent='Uploading... '+p+'%';}};
  x.onload=function(){bar.style.width='100%';st.textContent=(x.status===200)?'Done - device restarting...':'Upload failed ('+x.status+')';if(x.status!==200)btn.disabled=false;};
  x.onerror=function(){st.textContent='Upload failed';btn.disabled=false;};
  x.send(fd);
};
</script>
</body>
</html>
)HTML";

static const char OTA_CREDENTIALS_SAVED_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial - Saved</title>
<style>body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:60px 16px}
h1{color:#4CAF50}p{color:#888;margin-top:12px;line-height:1.6}</style>
</head>
<body>
<h1>&#10003; Saved!</h1>
<p>Device is restarting and joining your WiFi network.<br>
Open <strong>fluiddial.local</strong> in your browser to access OTA updates.</p>
</body>
</html>
)HTML";

// ─── Captive portal HTML ──────────────────────────────────────────────────────

static const char SETUP_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial WiFi Setup</title>
<style>
  *{box-sizing:border-box}
  body{font-family:sans-serif;max-width:420px;margin:32px auto;padding:16px;
       background:#1a1a1a;color:#eee}
  h2{color:#4CAF50;margin-bottom:4px}
  p.sub{color:#aaa;font-size:14px;margin-bottom:20px}
  label{display:block;margin:14px 0 4px;color:#ccc;font-size:14px}
  input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #555;
        background:#2a2a2a;color:#eee;font-size:16px}
  input:focus,select:focus{outline:none;border-color:#4CAF50}
  .row{display:flex;gap:8px}
  .row input{flex:1}
  .scan-btn{padding:10px 14px;background:#2a2a2a;color:#4CAF50;border:1px solid #4CAF50;
            border-radius:6px;font-size:14px;cursor:pointer;white-space:nowrap;font-weight:bold}
  .scan-btn:hover{background:#1e3d1e}
  .scan-btn:disabled{color:#555;border-color:#555;cursor:default}
  #netList{display:none;margin-top:6px}
  button[type=submit]{margin-top:24px;width:100%;padding:14px;background:#4CAF50;color:#fff;
         border:none;border-radius:6px;font-size:18px;cursor:pointer;font-weight:bold}
  button[type=submit]:hover{background:#45a049}
  .note{margin-top:16px;font-size:13px;color:#888;text-align:center}
  .scan-status{font-size:13px;color:#aaa;margin-top:4px;min-height:18px}
  .eye-btn{padding:10px 12px;background:#2a2a2a;color:#aaa;border:1px solid #555;
           border-radius:6px;font-size:18px;cursor:pointer;line-height:1}
  .eye-btn:hover{color:#4CAF50;border-color:#4CAF50}
</style>
</head>
<body>
<h2>FluidDial WiFi Setup</h2>
<p class="sub">Connect the FluidDial pendant to your WiFi network and FluidNC machine.</p>
<form method="POST" action="/save">
  <label>WiFi Network Name (SSID)</label>
  <div class="row">
    <input type="text" name="ssid" id="ssid" placeholder="YourNetworkName" autocomplete="off" required value="%SSID_VAL%">
    <button type="button" class="scan-btn" id="scanBtn" onclick="doScan()">Scan</button>
  </div>
  <select id="netList" onchange="pickNet(this)">
    <option value="">-- select a network --</option>
  </select>
  <div id="scanStatus" class="scan-status"></div>
  <label>WiFi Password</label>
  <div class="row">
    <input type="text" name="pass" id="pass" placeholder="Leave blank for open networks">
    <button type="button" class="eye-btn" id="eyeBtn" onclick="togglePass()">&#x1F441;</button>
  </div>
  <label>FluidNC Address (IP or hostname)</label>
  <input type="text" name="ip" placeholder="192.168.1.100 or fluidnc.local"
         autocomplete="off" required value="%IP_VAL%">
  <button type="submit">Save &amp; Connect</button>
</form>
<p class="note">The pendant will restart and connect automatically.</p>
<script>
function doScan(){
  var btn=document.getElementById('scanBtn');
  var lst=document.getElementById('netList');
  var st=document.getElementById('scanStatus');
  btn.disabled=true; btn.textContent='Scanning...';
  st.textContent='Scanning for networks, please wait...';
  lst.style.display='none';
  pollScan();
}
function pollScan(){
  fetch('/scan').then(function(r){
    if(r.status===202){setTimeout(pollScan,1500);return null;}
    return r.json();
  }).then(function(nets){
    if(!nets)return;
    var lst=document.getElementById('netList');
    var st=document.getElementById('scanStatus');
    var btn=document.getElementById('scanBtn');
    lst.innerHTML='<option value="">-- select a network --</option>';
    nets.sort(function(a,b){return b.rssi-a.rssi;});
    nets.forEach(function(n){
      var o=document.createElement('option');
      o.value=n.ssid;
      o.textContent=n.ssid+(n.secure?' [secured]':'')+'  ('+n.rssi+' dBm)';
      lst.appendChild(o);
    });
    lst.style.display='block';
    st.textContent=nets.length+' network'+(nets.length!==1?'s':'')+' found.';
    btn.disabled=false; btn.textContent='Scan';
  }).catch(function(){
    document.getElementById('scanStatus').textContent='Scan failed. Try again.';
    document.getElementById('scanBtn').disabled=false;
    document.getElementById('scanBtn').textContent='Scan';
  });
}
function pickNet(sel){
  if(sel.value) document.getElementById('ssid').value=sel.value;
}
function togglePass(){
  var p=document.getElementById('pass');
  var b=document.getElementById('eyeBtn');
  if(p.type==='text'){p.type='password';b.style.color='#555';}
  else{p.type='text';b.style.color='#aaa';}
}
</script>
</body>
</html>
)HTML";

static const char SAVED_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial – Saved</title>
<style>
  body{font-family:sans-serif;max-width:420px;margin:80px auto;padding:16px;
       background:#1a1a1a;color:#eee;text-align:center}
  h2{color:#4CAF50}p{color:#aaa}
</style></head>
<body>
<h2>Settings Saved!</h2>
<p>FluidDial is restarting and will connect to your network shortly.</p>
</body>
</html>
)HTML";

// --- OTA HTML ---
// Served over STA WiFi at fluiddial.local (or device IP).
// Browser is on the same network and has internet - it fetches GitHub directly.

static const char OTA_HTML[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FluidDial Firmware</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#111;color:#eee;padding:16px;max-width:500px;margin:0 auto}
h1{color:#4CAF50;font-size:20px;margin-bottom:4px}
.sub{color:#888;font-size:13px;margin-bottom:18px}
.card{background:#1e1e1e;border:1px solid #333;border-radius:8px;padding:14px;margin-bottom:12px}
h2{font-size:12px;color:#666;margin-bottom:10px;text-transform:uppercase;letter-spacing:.5px}
.info{display:flex;justify-content:space-between;font-size:13px;margin-bottom:5px}
.lbl{color:#666}.val{color:#eee}
.row{display:flex;justify-content:space-between;align-items:center;padding:8px 10px;border-radius:6px;background:#2a2a2a;margin-bottom:6px}
.tag{font-size:15px;font-weight:bold}.tag.cur{color:#4CAF50}
.right{display:flex;align-items:center;gap:8px}
.badge{font-size:11px;padding:2px 7px;border-radius:4px;background:#0d2d0d;color:#4CAF50;border:1px solid #1a4a1a}
.ibtn{padding:5px 12px;border:1px solid #4CAF50;background:transparent;color:#4CAF50;border-radius:5px;cursor:pointer;font-size:13px}
.ibtn:hover{background:#1a3a1a}.ibtn:disabled{border-color:#444;color:#444;cursor:default}
.msg{font-size:13px;color:#888;text-align:center;padding:10px 0}
#pw{display:none}
.bar-bg{background:#222;border-radius:4px;height:18px;margin:8px 0;border:1px solid #444;overflow:hidden}
#bar{height:100%;background:#4CAF50;width:0%;transition:width .2s}
#pct{text-align:center;font-size:13px;color:#888}
input[type=file]{width:100%;padding:8px;border:1px solid #444;border-radius:6px;background:#222;color:#eee;font-size:13px;margin:8px 0}
.primary{width:100%;padding:11px;background:#4CAF50;color:#fff;border:none;border-radius:6px;font-size:15px;cursor:pointer;font-weight:bold;margin-top:4px}
.primary:hover{background:#45a049}.primary:disabled{background:#1a3a1a;color:#555;cursor:default}
.spin{display:inline-block;width:14px;height:14px;border:2px solid #333;border-top-color:#4CAF50;border-radius:50%;animation:sp .8s linear infinite;vertical-align:middle;margin-right:6px}
@keyframes sp{to{transform:rotate(360deg)}}
.nbtn{padding:5px 10px;border:1px solid #555;background:transparent;color:#aaa;border-radius:5px;cursor:pointer;font-size:13px}
.nbtn:hover{border-color:#888;color:#ddd}
.notes{display:none;background:#161616;border:1px solid #333;border-radius:6px;margin:-2px 0 8px;padding:10px 12px;font-size:13px;color:#bbb;line-height:1.5;overflow-wrap:anywhere}
.notes h1,.notes h2,.notes h3,.notes h4{color:#ddd;font-size:14px;margin:8px 0 4px;text-transform:none;letter-spacing:0}
.notes ul,.notes ol{margin:4px 0 4px 18px}.notes li{margin:2px 0}
.notes a{color:#4CAF50}.notes p{margin:4px 0}
.notes code{background:#000;padding:1px 4px;border-radius:3px;font-size:12px}
.notes pre{background:#000;padding:8px;border-radius:4px;overflow-x:auto;white-space:pre-wrap}
</style>
<script src="https://cdn.jsdelivr.net/npm/marked/marked.min.js" defer></script>
</head>
<body>
<h1>FluidDial Firmware</h1>
<p class="sub">Update the device firmware.</p>

<div class="card">
  <h2>Device</h2>
  <div class="info"><span class="lbl">Version</span><span class="val" id="cur">...</span></div>
</div>

<div class="card">
  <h2>Official Releases</h2>
  <div id="rb"><div class="msg"><span class="spin"></span>Loading from GitHub...</div></div>
</div>

<div id="pw" class="card">
  <h2 id="pt">Working...</h2>
  <div class="bar-bg"><div id="bar"></div></div>
  <p id="pct">0%</p>
</div>

<div class="card">
  <h2>Manual Upload</h2>
  <p class="sub" style="margin-bottom:0">Upload an app-only image (<strong>firmware.bin</strong> from the build), not the merged/full-flash image.</p>
  <form id="uf">
    <input type="file" id="ufile" accept=".bin" required>
    <button class="primary" type="submit" id="ubtn">Upload</button>
  </form>
</div>

<script>
var busy=false,board='',curVer='',rels=[];
var API='https://api.github.com/repos/bdring/FluidDial/releases';
var RAW='https://raw.githubusercontent.com/bdring/fluiddial-releases/main/releases';

function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function setBar(p){document.getElementById('bar').style.width=p+'%';document.getElementById('pct').textContent=p+'%';}
function showProg(t){document.getElementById('pw').style.display='block';document.getElementById('pt').textContent=t;setBar(0);}

fetch('/releases').then(r=>r.json()).then(d=>{
  curVer=d.current||'?'; board=d.board||'';
  document.getElementById('cur').textContent=curVer;
}).catch(()=>{}).finally(loadGitHub);

function loadGitHub(){
  fetch(API,{headers:{'Accept':'application/vnd.github+json'}})
    .then(r=>r.json()).then(function(releases){
      var list=releases.filter(r=>!r.draft&&!r.prerelease).slice(0,10);
      if(!list.length){document.getElementById('rb').innerHTML='<div class="msg">No releases found.</div>';return;}
      var curBase=(curVer.match(/^[\d.]+/)||[''])[0];
      var h=''; rels=[];
      list.forEach(function(r,i){
        var tag=r.tag_name||r.name,isCur=(tag===curBase);
        rels.push({tag:tag,body:r.body||''});
        h+='<div class="row"><span class="tag'+(isCur?' cur':'')+'">'+esc(tag)+'</span>';
        h+='<span class="right">'+(isCur?'<span class="badge">installed</span>':'')+
           '<button class="nbtn" onclick="toggleNotes('+i+',this)">Notes</button>'+
           '<button class="ibtn" onclick="inst(\''+esc(tag)+'\')">Install</button></span></div>';
        h+='<div class="notes" id="n'+i+'"></div>';
      });
      document.getElementById('rb').innerHTML=h;
    }).catch(function(){
      document.getElementById('rb').innerHTML=
        '<div class="msg" style="color:#f80">Could not reach GitHub.</div>'+
        '<button onclick="loadGitHub()" style="margin-top:8px;width:100%;padding:8px;border:1px solid #4CAF50;background:transparent;color:#4CAF50;border-radius:5px;cursor:pointer">Retry</button>';
    });
}

// Render a release's markdown notes. Uses marked (CDN) when present; otherwise
// falls back to escaped plaintext so notes still show without the library.
function renderMd(md){
  if(!md||!md.trim())return '<p style="color:#666">No release notes.</p>';
  if(window.marked){try{return marked.parse?marked.parse(md):marked(md);}catch(e){}}
  return '<pre>'+esc(md)+'</pre>';
}
function toggleNotes(i,btn){
  var el=document.getElementById('n'+i);
  if(!el)return;
  if(el.style.display==='block'){el.style.display='none';btn.textContent='Notes';return;}
  if(!el.dataset.rendered){el.innerHTML=renderMd(rels[i].body);el.dataset.rendered='1';}
  el.style.display='block'; btn.textContent='Hide';
}

// XHR download with progress callback (returns Promise<ArrayBuffer>)
function xhrGet(url,onProg){
  return new Promise(function(res,rej){
    var x=new XMLHttpRequest(); x.open('GET',url); x.responseType='arraybuffer';
    x.onprogress=function(e){if(e.lengthComputable&&onProg)onProg(e.loaded/e.total);};
    x.onload=function(){x.status===200?res(x.response):rej('HTTP '+x.status);};
    x.onerror=function(){rej('Network error');};
    x.send();
  });
}

// Extract the app image out of a merged full-flash image. The merged image is
// bootloader@0x0 + partition table@0x8000 + app@0x10000 + LittleFS (much later).
// OTA flashes straight into the app OTA partition, so we must hand it just the
// app — not the bootloader before it nor the filesystem after it. The app's
// exact byte length is self-described by the ESP32 image header (header + N
// segments + checksum + optional appended SHA-256), so we compute it precisely
// instead of guessing, which is what lets us reuse the published merged image
// for OTA without building a separate app-only artifact.
function extractApp(ab){
  var APP_OFF=0x10000;
  var b=new Uint8Array(ab);
  if(b.length<APP_OFF+24||b[APP_OFF]!==0xE9)throw 'Bad app image (no 0xE9 at 0x10000)';
  var segCount=b[APP_OFF+1];
  var hashAppended=b[APP_OFF+23]===1;
  var p=APP_OFF+24;                                 // past 24-byte image header
  for(var i=0;i<segCount;i++){
    var len=(b[p+4]|(b[p+5]<<8)|(b[p+6]<<16)|(b[p+7]<<24))>>>0; // data_len, LE
    p+=8+len;                                       // 8-byte seg header + data
  }
  var imgLen=p-APP_OFF;
  imgLen+=(16-((imgLen+1)%16))%16;                  // pad so checksum is 16-aligned
  imgLen+=1;                                         // checksum byte
  if(hashAppended)imgLen+=32;                        // appended SHA-256
  if(APP_OFF+imgLen>b.length)throw 'App image overruns download';
  return ab.slice(APP_OFF,APP_OFF+imgLen);
}

// Numeric dotted-version compare: returns -1/0/1 for a<b / a==b / a>b.
function verCmp(a,b){
  var pa=String(a).split('.').map(Number),pb=String(b).split('.').map(Number);
  for(var i=0;i<Math.max(pa.length,pb.length);i++){
    var x=pa[i]||0,y=pb[i]||0;
    if(x<y)return -1; if(x>y)return 1;
  }
  return 0;
}

function inst(tag){
  if(busy||!board)return;
  // Versions before 1.3.1 have no OTA support — installing one removes the
  // ability to update over WiFi, leaving USB reflashing as the only way back.
  var v=(String(tag).match(/\d+(\.\d+)*/)||['0'])[0];
  if(verCmp(v,'1.3.1')<0){
    if(!confirm('Version '+tag+' does not support over-the-air (OTA) updates.\n\n'+
                'If you install it, OTA updates will not be available and '+
                'you subsequent updates will require a USB connection.\n\nInstall anyway?'))return;
  }
  busy=true;
  showProg('Loading manifest...');
  // 1. Fetch the release manifest to find this board's published image.
  xhrGet(RAW+'/'+tag+'/manifest.json',null)
    .then(function(buf){
      var manifest=JSON.parse(new TextDecoder().decode(buf));
      // The published image is the merged full-flash image (offset 0x0000); we
      // carve the app partition out of it client-side (see extractApp).
      var imgInfo=manifest.images&&manifest.images[board];
      if(!imgInfo)throw 'No image for board: '+board;
      // 2. Download the merged image.
      document.getElementById('pt').textContent='Downloading '+tag+'...';
      return xhrGet(RAW+'/'+tag+'/'+imgInfo.path,function(p){
        setBar(Math.round(p*50));
        document.getElementById('pt').textContent='Downloading '+tag+'... '+Math.round(p*100)+'%';
      });
    })
    .then(function(buf){
      // 3. Extract the app and upload only that.
      uploadBin(extractApp(buf),tag);
    })
    .catch(function(e){
      document.getElementById('pt').textContent='Failed: '+String(e);
      document.getElementById('pct').style.color='#f55'; busy=false;
    });
}

function uploadBin(buf,label){
  document.getElementById('pt').textContent='Uploading to device...'; setBar(50);
  var fd=new FormData();
  fd.append('firmware',new Blob([buf],{type:'application/octet-stream'}),'firmware.bin');
  var x=new XMLHttpRequest(); x.open('POST','/update');
  x.upload.onprogress=function(e){if(e.lengthComputable)setBar(50+Math.round(e.loaded/e.total*50));};
  x.onload=function(){setBar(100);document.getElementById('pt').textContent='Done - device restarting...';};
  x.onerror=function(){document.getElementById('pt').textContent='Upload failed';busy=false;};
  x.send(fd);
}

document.getElementById('uf').onsubmit=function(e){
  e.preventDefault();
  var f=document.getElementById('ufile').files[0];
  if(!f||busy)return; busy=true; showProg('Uploading '+f.name+'...');
  var fd=new FormData(); fd.append('firmware',f);
  var x=new XMLHttpRequest(); x.open('POST','/update');
  x.upload.onprogress=function(ev){if(ev.lengthComputable)setBar(Math.round(ev.loaded/ev.total*100));};
  x.onload=function(){setBar(100);document.getElementById('pt').textContent='Done - device restarting...';};
  x.onerror=function(){document.getElementById('pt').textContent='Upload failed';busy=false;};
  document.getElementById('ubtn').disabled=true; x.send(fd);
};
</script>
</body>
</html>
)HTML";

// ─── HTTP request handlers ────────────────────────────────────────────────────

static String htmlEscape(const String& input) {
    String escaped;
    escaped.reserve(input.length());
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '&':  escaped += F("&amp;");  break;
            case '<':  escaped += F("&lt;");   break;
            case '>':  escaped += F("&gt;");   break;
            case '"':  escaped += F("&quot;"); break;
            case '\'': escaped += F("&#39;");  break;
            default:   escaped += c;           break;
        }
    }
    return escaped;
}

static void handleRoot() {
    if (_ota_server_active && _ota_ap_mode) {
        httpServer.send(200, "text/html", OTA_CREDENTIALS_HTML);
        return;
    }
    WiFiConfig cfg = wifi_load_config();
    String page = SETUP_HTML;
    page.replace("%SSID_VAL%", cfg.valid ? htmlEscape(String(cfg.ssid)) : "");
    // Prefill the FluidNC address with "fluidnc.local" when none is stored, so a
    // first-time user has a working default instead of an
    // empty required field.
    String ipVal = (cfg.valid && cfg.fluidnc_ip[0])
                       ? htmlEscape(String(cfg.fluidnc_ip))
                       : "fluidnc.local";
    page.replace("%IP_VAL%", ipVal);
    httpServer.send(200, "text/html", page);
}

static void handleSave() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    String ip   = httpServer.arg("ip");

    ssid.trim();
    pass.trim();
    ip.trim();

    String cleanIp;
    for (int i = 0; i < (int)ip.length(); i++) {
        char c = ip[i];
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || c == '.' || c == '-') {
            cleanIp += c;
        }
    }
    ip = cleanIp;

    if (ssid.length() == 0 || ip.length() == 0) {
        httpServer.send(400, "text/plain", "SSID and IP are required");
        return;
    }

    wifi_save_config(ssid.c_str(), pass.c_str(), ip.c_str());
    httpServer.send(200, "text/html", SAVED_HTML);

    delay(2000);
    ESP.restart();
}

static void handleScan() {
    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) {
        httpServer.sendHeader("Cache-Control", "no-cache");
        httpServer.send(202, "application/json", "[]");
        return;
    }

    if (n == WIFI_SCAN_FAILED || n < 0) {
        WiFi.scanNetworks(true, false);
        httpServer.sendHeader("Cache-Control", "no-cache");
        httpServer.send(202, "application/json", "[]");
        return;
    }

    String json = "[";
    for (int i = 0; i < n && i < 32; i++) {
        if (i > 0) json += ",";
        String ssid = WiFi.SSID(i);
        String safe;
        safe.reserve(ssid.length());
        for (int j = 0; j < (int)ssid.length(); j++) {
            char c = ssid[j];
            if (c == '\\' || c == '"') safe += '\\';
            if (c >= 0x20) safe += c;  // drop control characters
        }
        json += "{\"ssid\":\"";
        json += safe;
        json += "\",\"rssi\":";
        json += String(WiFi.RSSI(i));
        json += ",\"secure\":";
        json += (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false";
        json += "}";
    }
    json += "]";
    WiFi.scanDelete();

    httpServer.sendHeader("Cache-Control", "no-cache");
    httpServer.send(200, "application/json", json);
}

static void handleNotFound() {
    // Captive portal: redirect to setup page.
    httpServer.sendHeader("Location", "http://192.168.4.1/", true);
    httpServer.send(302, "text/plain", "");
}

static void handleOtaRoot() {
    if (_ota_ap_mode) {
        httpServer.send(200, "text/html", OTA_CREDENTIALS_HTML);
        return;
    }
    httpServer.send(200, "text/html", OTA_HTML);
}

static void handleOtaUploadDone() {
    if (Update.hasError()) {
        httpServer.send(500, "text/plain", "Update failed");
        _ota_progress = -1;
        request_redisplay();
    } else {
        httpServer.send(200, "text/plain", "OK");
        _ota_progress = 100;
        request_redisplay();
        delay(1500);
        ESP.restart();
    }
}

static size_t _ota_content_length = 0;

static void handleOtaUploadData() {
    HTTPUpload& upload = httpServer.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String cl = httpServer.header("Content-Length");
        _ota_content_length = cl.length() ? (size_t)cl.toInt() : 0;
        dbg_printf("OTA: upload start — %s (content-length=%u)\n",
                   upload.filename.c_str(), (unsigned)_ota_content_length);
        _ota_progress = 1;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            dbg_println("OTA: Update.begin() failed");
            _ota_progress = -1;
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_ota_progress >= 0) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                dbg_println("OTA: Update.write() failed");
                _ota_progress = -1;
            } else if (_ota_content_length > 0) {
                _ota_progress = (int)((upload.totalSize * 99ULL) / _ota_content_length) + 1;
                if (_ota_progress > 99) _ota_progress = 99;
            }
        }
        request_redisplay();
    } else if (upload.status == UPLOAD_FILE_END) {
        if (_ota_progress >= 0 && !Update.end(true)) {
            dbg_println("OTA: Update.end() failed");
            _ota_progress = -1;
        }
        dbg_printf("OTA: upload end — %u bytes total\n", (unsigned)upload.totalSize);
        request_redisplay();
    }
}

static void handleOtaWifiSave() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
        httpServer.send(400, "text/plain", "SSID required");
        return;
    }
    WiFiConfig existing = wifi_load_config();
    
    const char* ip = (existing.valid && existing.fluidnc_ip[0])
                         ? existing.fluidnc_ip
                         : "fluidnc.local";
    wifi_save_config(ssid.c_str(), pass.c_str(), ip);
    httpServer.send(200, "text/html", OTA_CREDENTIALS_SAVED_HTML);
    delay(1500);
    // Re-arm OTA to reboot straight back into the OTA server (now with credentials) + serve fluiddial.local without the user
    // having to re-open the OTA menu.
    wifi_request_ota_reboot();
}

static void handleGetReleases() {
    String j = "{\"current\":\""; j += git_info; j += "\",\"board\":\"";
#ifdef USE_M5
    j += "m5dial";
#else
    j += "cyddial";
#endif
    j += "\"}";
    httpServer.sendHeader("Cache-Control", "no-cache");
    httpServer.send(200, "application/json", j);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void wifi_save_config(const char* ssid, const char* password, const char* ip) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putString("ip",   ip);
    prefs.end();
    dbg_printf("WiFi config saved: ssid=%s ip=%s\n", ssid, ip);
}

WiFiConfig wifi_load_config() {
    WiFiConfig cfg = {};

#if HARDCODE_TEST_WIFI
    strncpy(cfg.ssid, TEST_WIFI_SSID, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, TEST_WIFI_PASS, sizeof(cfg.password) - 1);
    strncpy(cfg.fluidnc_ip, TEST_FLUIDNC_IP, sizeof(cfg.fluidnc_ip) - 1);
    cfg.valid = true;
    return cfg;
#endif

    Preferences prefs;
    // Open read-write so the namespace is created on first boot, avoiding
    // the NVS_NOT_FOUND error that occurs with read-only on a fresh device.
    prefs.begin(PREF_NAMESPACE, false);
    String ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
    String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
    String ip   = prefs.isKey("ip")   ? prefs.getString("ip",   "") : "";
    prefs.end();

    if (ssid.length() > 0) {
        dbg_printf("NVS loaded: ssid='%s' (len=%d) ip='%s' (len=%d)\n",
                   ssid.c_str(), ssid.length(), ip.c_str(), ip.length());
        strncpy(cfg.ssid,      ssid.c_str(), sizeof(cfg.ssid)      - 1);
        strncpy(cfg.password,  pass.c_str(), sizeof(cfg.password)  - 1);
        strncpy(cfg.fluidnc_ip, ip.c_str(), sizeof(cfg.fluidnc_ip) - 1);
        cfg.valid = true;
    }
    return cfg;
}

void wifi_start_ap_setup() {
    _ap_mode            = true;
    tcp_close();
    _ws_started         = false;
    _wifi_stack_started = true;  // ensure wifi_poll() drives DNS/HTTP server

    WiFi.disconnect(true);
    delay(100);
    // WIFI_AP_STA keeps the STA interface active so WiFi.scanNetworks() works
    // while the AP is running (pure WIFI_AP disables the STA scan engine).
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID, (strlen(WIFI_AP_PASS) ? WIFI_AP_PASS : nullptr));

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    // DNS: redirect every domain to the AP gateway (captive portal).
    dnsServer.start(53, "*", apIP);

    httpServer.on("/",      HTTP_GET,  handleRoot);
    httpServer.on("/generate_204", HTTP_GET, handleRoot);
    httpServer.on("/hotspot-detect.html", HTTP_GET, handleRoot);
    httpServer.on("/ncsi.txt", HTTP_GET, handleRoot);
    httpServer.on("/fwlink", HTTP_GET, handleRoot);
    httpServer.on("/scan",  HTTP_GET,  handleScan);
    httpServer.on("/save",  HTTP_POST, handleSave);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();

    dbg_printf("AP started — SSID: %s  IP: %s\n",
               WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void wifi_stop_ap_and_restart() {
    httpServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _ap_mode = false;
    delay(300);
    ESP.restart();
}

void wifi_stop_ap() {
    httpServer.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    _ap_mode            = false;
    _wifi_stack_started = false;

    // Re-initiate the STA connection that was torn down by wifi_start_ap_setup().
    // Pass auto_ap=false so that a device with no credentials does not immediately
    // re-enter AP mode — the user explicitly chose to exit.
    wifi_init(false);
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool websocket_is_connected() {
    return _ws_connected;
}

void wifi_force_ws_reconnect() {
    if (_ws_started) {
        tcp_close();
        _tcp_next_try_ms = millis() + TCP_RECONNECT_DELAY_MS;
        dbg_println("Telnet: force-closed for reconnect");
    }
}

// Gracefully tear down the network before leaving WiFi mode
void wifi_shutdown() {
    _ws_started = false;
    tcp_close();
    delay(60);                  // flush the FIN
    WiFi.disconnect(true, false);
    delay(20);
    dbg_println("WiFi: shut down for transport switch");
}

bool wifi_in_ap_mode() {
    return _ap_mode;
}

const char* wifi_ap_ssid() {
    return WIFI_AP_SSID;
}

const char* wifi_status_str() {
    if (wifi_use_uart_mode())   return "UART Mode";
    if (wifi_use_espnow_mode()) return "ESP-NOW Mode";
    if (_ap_mode)               return "AP Setup Mode";
    if (!wifi_is_connected())   return "Connecting to WiFi";
    if (!_ws_connected)         return "Connecting to FluidNC";
    return "Connected";
}

const bool wifi_not_ready() {

    return (!wifi_is_connected() || !_ws_connected);
}

const char* wifi_last_error() {
    return _wifi_error_msg;
}

// Returns the config that was loaded at wifi_init() time — no NVS read.
WiFiConfig wifi_active_config() {
    return _active_cfg;
}

int wifi_signal_bars() {
    if (!wifi_is_connected()) return 0;
    int rssi = WiFi.RSSI();
    if (rssi >= -55) return 4;   // Excellent
    if (rssi >= -65) return 3;   // Good
    if (rssi >= -75) return 2;   // Fair
    if (rssi >= -85) return 1;   // Weak
    return 0;                    // Very weak
}

// ─── WiFi event handler ───────────────────────────────────────────────────────
// Called from the WiFi task — only set a flag; UI work happens in wifi_poll().

static void onWiFiDisconnect(WiFiEvent_t event, WiFiEventInfo_t info) {
    _wifi_disconnect_reason = info.wifi_sta_disconnected.reason;
}

static void ensure_wifi_event_registered() {
    static bool registered = false;
    if (!registered) {
        WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        registered = true;
    }
}

void wifi_init(bool auto_ap) {
    TransportMode transport = wifi_get_transport();
    if (transport == TransportMode::UART) {
        dbg_println("Transport: UART mode — WiFi stack not started");
        return;
    }
    if (transport == TransportMode::ESPNOW) {
        dbg_println("Transport: ESP-NOW mode — WebSocket stack not started");
        return;
    }
    if (wifi_is_first_boot()) {
        dbg_println("First boot — WiFi deferred until transport is selected");
        return;
    }

    WiFiConfig cfg = wifi_load_config();

    if (!cfg.valid) {
        if (auto_ap) {
            // No credentials saved yet — auto-start AP so the user can configure
            // via browser immediately (captive portal at 192.168.4.1).
            dbg_println("No WiFi credentials — starting AP setup mode automatically");
            wifi_start_ap_setup();
            request_redisplay();  // switch scene from "not configured" to AP view
        }
        return;
    }

    _wifi_stack_started = true;
    dbg_printf("Connecting to WiFi: %s  FluidNC: %s\n", cfg.ssid, cfg.fluidnc_ip);
    _active_cfg              = cfg;
    tcp_close();
    _ws_started              = false;
    _tcp_next_try_ms         = 0;
    _fluidnc_remote_ip[0]    = '\0';
    _wifi_was_connected      = false;
    _last_wifi_status        = WL_IDLE_STATUS;
    _wifi_error_msg          = nullptr;
    _wifi_retry_at           = 0;
    _wifi_disconnect_reason  = 0;
    _wifi_ever_connected     = false;
    _wifi_connect_start_ms   = 0;  // set after WiFi.begin() below
    _handshake_timeout_count = 0;
    _dns_retry_at            = 0;

    ensure_wifi_event_registered();

    WiFi.persistent(false);
    WiFi.disconnect(true);  // Ensure clean driver state (especially after AP mode).
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    WiFi.setAutoReconnect(false);
    WiFi.begin(cfg.ssid, cfg.password[0] ? cfg.password : nullptr);
    _wifi_connect_start_ms = millis();
}

void wifi_poll() {
    if (_ota_server_active) {
        if (_ota_ap_mode) {
            dnsServer.processNextRequest();
        } else {
            wl_status_t sta = WiFi.status();
            if (sta != _ota_last_sta) {
                _ota_last_sta = sta;
                if (sta == WL_CONNECTED) {
                    _ota_error_msg = nullptr;
                    MDNS.end();
                    MDNS.begin("fluiddial");
                    strncpy(_ota_ip_str, WiFi.localIP().toString().c_str(),
                            sizeof(_ota_ip_str) - 1);
                    dbg_printf("OTA: STA connected — %s (fluiddial.local)\n", _ota_ip_str);
                }
                request_redisplay();
            }
            // Surface why a connect is failing so the pendant can offer to
            // re-enter credentials instead of spinning on "Connecting..." forever.
            if (sta != WL_CONNECTED && !_ota_error_msg) {
                if (_wifi_disconnect_reason) {
                    uint8_t reason          = _wifi_disconnect_reason;
                    _wifi_disconnect_reason = 0;
                    if (reason == WIFI_REASON_AUTH_FAIL   ||
                        reason == WIFI_REASON_AUTH_EXPIRE ||
                        reason == WIFI_REASON_MIC_FAILURE) {
                        _ota_error_msg = "Check password";
                        request_redisplay();
                    }
                }
                if (!_ota_error_msg && _ota_connect_start_ms &&
                    (millis() - _ota_connect_start_ms) > 15000) {
                    _ota_error_msg = "Cannot connect";
                    request_redisplay();
                }
            }
        }
        httpServer.handleClient();
        return;
    }

    if (!_wifi_stack_started) return;  // wifi_init() never ran (first boot or UART/ESP-NOW mode)
    if (wifi_use_uart_mode())   return;
    if (wifi_use_espnow_mode()) return;

    if (_ap_mode) {
        dnsServer.processNextRequest();
        httpServer.handleClient();
        return;
    }

    wl_status_t wifi_status = WiFi.status();
    if (wifi_status != _last_wifi_status) {
        dbg_printf("WiFi status: %s (%d)\n", wifi_status_name(wifi_status), wifi_status);
        _last_wifi_status = wifi_status;

        if (wifi_status == WL_CONNECTED) {
            _wifi_ever_connected       = true;
            _wifi_error_msg            = nullptr;
            _wifi_retry_at             = 0;
            _wifi_connect_start_ms     = 0;
            _handshake_timeout_count   = 0;
            _wifi_disconnect_reason = 0;
            // Re-arm auto-reconnect now that we're up; it was disabled on any
            // prior failure so drops after a successful session still recover.
            WiFi.setAutoReconnect(true);
        }
    }

    // Early-timeout: show a generic message while the handshake is still in progress
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 8000;
    if (!_wifi_ever_connected && !_wifi_error_msg && _wifi_connect_start_ms &&
        (millis() - _wifi_connect_start_ms) > WIFI_CONNECT_TIMEOUT_MS) {
        _wifi_error_msg = "Cannot connect";
        request_redisplay();
    }

    // Decode the reason code set by the WiFi disconnect event.
    static const char* const MSG_CHECK_PASS = "Check password";
    static const char* const MSG_NOT_FOUND  = "Network not found";

    if (_wifi_disconnect_reason) {
        uint8_t reason          = _wifi_disconnect_reason;
        _wifi_disconnect_reason = 0;
        dbg_printf("WiFi disconnect reason: %d\n", reason);

        if (!_wifi_ever_connected) {
            const char* new_msg = nullptr;
            bool        stop_driver = false;
            bool        allow_retry = false;

            if (reason == WIFI_REASON_AUTH_FAIL   ||
                reason == WIFI_REASON_AUTH_EXPIRE  ||
                reason == WIFI_REASON_MIC_FAILURE) {                new_msg     = MSG_CHECK_PASS;
                stop_driver = true;
                allow_retry = false;
            } else if (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
                // Handle cold-boot handshake timeout.
                // Retry up to 3 times before concluding it is an actual auth failure.
                _handshake_timeout_count++;
                if (_handshake_timeout_count >= 3) {
                    new_msg     = MSG_CHECK_PASS;
                    stop_driver = true;
                    allow_retry = false;
                } else {
                    // Silent retry after a short delay — do not show an error yet.
                    stop_driver = false;
                    allow_retry = true;
                    if (!_wifi_retry_at) {
                        _wifi_retry_at = millis() + 2000;
                    }
                }
            } else if (reason == WIFI_REASON_NO_AP_FOUND) {
                // NO_AP_FOUND fires spuriously during retry rescans even when
                // the AP is present but rejecting credentials — only show
                // it if not already identified an auth failure.
                if (_wifi_error_msg != MSG_CHECK_PASS) {
                    new_msg     = MSG_NOT_FOUND;
                    stop_driver = true;   // stop the driver cycle
                    allow_retry = true;   // network might come back; retry later
                }
            }

            if (new_msg) {
                if (stop_driver) {
                    WiFi.setAutoReconnect(false);
                    WiFi.disconnect(false);
                }
                bool changed = (_wifi_error_msg != new_msg);
                _wifi_error_msg = new_msg;
                // Only arm a retry for not-found; never for wrong password.
                if (allow_retry && !_wifi_retry_at) {
                    _wifi_retry_at = millis() + WIFI_RETRY_DELAY_MS;
                }
                if (changed) request_redisplay();
            }
        }
    }

    // Re-issue WiFi.begin() after a not-found failure (auth failures never retry).
    if (_wifi_retry_at && millis() >= _wifi_retry_at) {
        _wifi_retry_at         = 0;
        _wifi_error_msg        = nullptr;
        _last_wifi_status      = WL_IDLE_STATUS;
        _wifi_connect_start_ms = millis();
        dbg_printf("WiFi retry: reconnecting to %s\n", _active_cfg.ssid);
        WiFi.setAutoReconnect(false);  // keep manual control; re-enabled on WL_CONNECTED
        WiFi.disconnect(false);
        delay(100);
        WiFi.begin(_active_cfg.ssid, _active_cfg.password[0] ? _active_cfg.password : nullptr);
    }

    // Detect WiFi reconnects so the Telnet socket can be re-opened.
    bool now_connected = wifi_status == WL_CONNECTED;
    if (now_connected && !_wifi_was_connected) {
        dbg_printf("WiFi connected — IP: %s\n",
                   WiFi.localIP().toString().c_str());
        // Initialise mDNS so ".local" names resolve via the lwIP mDNS resolver.
        // Always call end() first — if WiFi reconnects (signal drop, router reboot,
        // etc.) without end(), each begin() leaks heap and a task, eventually crashing.
        MDNS.end();
        if (!MDNS.begin("fluiddial")) {
            dbg_println("mDNS init failed — .local hostnames may not resolve");
        }
        if (_active_cfg.fluidnc_ip[0] == '\0') {
            dbg_println("No FluidNC address configured — WiFi up, Telnet idle");
        } else if (is_dotted_decimal(_active_cfg.fluidnc_ip)) {
            // Plain IP address — open Telnet socket immediately.
            tcp_begin(_active_cfg.fluidnc_ip);
        } else if (!_dns_resolving) {
            // Hostname — resolve asynchronously; tcp_begin() runs when done.
            _dns_retry_at = 0;  // cancel any pending retry from a prior failure
            start_dns_resolve();
        }
        // WiFi just came up — update badge from "WiFi..." to "WiFi OK".
        request_redisplay();
    }
    if (!now_connected && _wifi_was_connected) {
        if (_ws_started) {
            tcp_close();
            _ws_started = false;
        }
        _dns_done = false;  // discard any in-flight DNS result
        set_disconnected_state();
        dbg_println("WiFi lost");
    }
    _wifi_was_connected = now_connected;

    // Handle async DNS completion (hostname -> IP resolution).
    // Only act if WiFi is still up and the Telnet socket hasn't already started.
    if (_dns_done && !_ws_started && now_connected) {
        std::atomic_thread_fence(std::memory_order_acquire);
        bool ok = _dns_ok;
        _dns_done = false;
        if (ok) {
            char resolved_host[sizeof(_dns_result_str)];
            memcpy(resolved_host, _dns_result_str, sizeof(resolved_host));
            dbg_printf("Hostname resolved: %s -> %s\n",
                       _active_cfg.fluidnc_ip, resolved_host);
            tcp_begin(resolved_host);
        } else {
            dbg_printf("Hostname resolution failed: %s — retrying in %d ms\n",
                       _active_cfg.fluidnc_ip, DNS_RETRY_DELAY_MS);
            _wifi_error_msg = "Host not found";
            _dns_retry_at   = millis() + DNS_RETRY_DELAY_MS;
        }
        request_redisplay();
    }

    // Retry DNS resolution after a failed attempt (ie: FluidNC may not have
    // started its mDNS responder yet when both devices boot together).
    if (_dns_retry_at && !_dns_resolving && !_ws_started && now_connected
        && millis() >= _dns_retry_at) {
        _dns_retry_at   = 0;
        _wifi_error_msg = nullptr;
        dbg_printf("DNS retry: resolving %s\n", _active_cfg.fluidnc_ip);
        start_dns_resolve();
        request_redisplay();
    }

    // Service the Telnet socket: refill RX from the kernel buffer, and if
    // the socket is down, attempt periodic reconnects.
    if (_ws_started && now_connected) {
        if (_connecting_fd >= 0) {
            tcp_poll_connect();
        } else if (_sock >= 0) {
            tcp_refill_rx();
            // RX-stall watchdog. A healthy FluidNC answers the 500 ms '?' polls,
            // so prolonged total silence on a live socket means it's wedged half-open
            if (_sock >= 0 && (int32_t)(millis() - _last_rx_ms) > WIFI_RX_STALL_MS) {
                dbg_println("Telnet: RX stalled — dropping wedged socket, reconnecting");
                tcp_close();
                set_disconnected_state();
                _tcp_next_try_ms = millis() + TCP_RECONNECT_DELAY_MS;
            }
        } else if (_tcp_next_try_ms && (int32_t)(millis() - _tcp_next_try_ms) >= 0) {
            _tcp_next_try_ms = 0;
            dbg_println("Telnet: reconnect attempt");
            if (!tcp_open(_fluidnc_remote_ip)) {
                _tcp_next_try_ms = millis() + TCP_RECONNECT_DELAY_MS;
            }
        }
    }

    // Animate the connecting badge until FluidNC is fully connected.
    if (!_ws_connected) {
        static uint32_t _last_anim_ms = 0;
        uint32_t        now_ms        = millis();
        if (now_ms - _last_anim_ms >= 400) {
            _last_anim_ms = now_ms;
            request_redisplay();
        }
        return;
    }

    // Explicit status poll every 500 ms.
    // (FluidNC auto-report via $RI is also set when state transitions from
    // Disconnected in show_state(), but we poll here as a belt-and-suspenders
    // measure until the first status arrives.)
    uint32_t now = millis();
    if (now - _last_status_ms >= STATUS_POLL_MS) {
        _last_status_ms = now;
        uint8_t qmark = '?';
        ws_send_bin(&qmark, 1);
    }
}

// --- OTA firmware update ---

static RTC_NOINIT_ATTR uint32_t _ota_boot_flag;
static constexpr uint32_t OTA_BOOT_MAGIC = 0x07A0B007;  // "OTA BOOT"

void wifi_request_ota_reboot() {
    _ota_boot_flag = OTA_BOOT_MAGIC;
    delay(40);          // let the display/log settle before the reset
    ESP.restart();
}

bool wifi_ota_boot_requested() {
    if (_ota_boot_flag == OTA_BOOT_MAGIC) {
        _ota_boot_flag = 0;
        return true;
    }
    return false;
}

static void start_ota_ap_credentials() {
    httpServer.stop();
    dnsServer.stop();

    _ota_progress   = 0;
    _ota_error_msg  = nullptr;
    _ota_ap_mode    = true;
    _ota_started_ap = true;
    _ap_mode        = true;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_AP_SSID, nullptr);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    dnsServer.start(53, "*", apIP);

    static const char* ota_ap_headers[] = { "Content-Length" };
    httpServer.collectHeaders(ota_ap_headers, 1);

    httpServer.on("/",                    HTTP_GET,  handleRoot);
    httpServer.on("/generate_204",        HTTP_GET,  handleRoot);
    httpServer.on("/hotspot-detect.html", HTTP_GET,  handleRoot);
    httpServer.on("/scan",                HTTP_GET,  handleScan);
    httpServer.on("/wifi-save",           HTTP_POST, handleOtaWifiSave);
    httpServer.on("/update",              HTTP_POST, handleOtaUploadDone, handleOtaUploadData);
    httpServer.onNotFound(handleNotFound);
    httpServer.begin();

    _ota_server_active = true;
    dbg_printf("OTA: AP started for credentials — SSID: %s\n", WIFI_AP_SSID);
}

void wifi_start_ota_server() {
    if (_ota_server_active) return;

    httpServer.stop();
    _ota_progress         = 0;
    _ota_content_length   = 0;
    _ota_last_sta         = WL_IDLE_STATUS;
    _ota_ap_mode          = false;
    _ota_started_ap       = false;
    _ota_started_sta      = false;
    _ota_error_msg        = nullptr;
    _ota_connect_start_ms = 0;

    WiFiConfig cfg = wifi_load_config();

    if (!cfg.valid) {
        // No WiFi credentials saved — collect them over an AP first. After the
        // user saves, the device restarts and re-enters STA mode below.
        start_ota_ap_credentials();
        return;
    }

    // Has credentials — connect STA and serve the OTA page at fluiddial.local.
    if (!wifi_is_connected()) {
        ensure_wifi_event_registered();   // so disconnect reasons are captured
        _wifi_disconnect_reason = 0;
        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.setAutoReconnect(true);      // self-heal transient handshake timeouts
        WiFi.begin(cfg.ssid, cfg.password[0] ? cfg.password : nullptr);
        _ota_started_sta      = true;
        _ota_connect_start_ms = millis();
        dbg_printf("OTA: connecting STA to %s\n", cfg.ssid);
    } else {
        strncpy(_ota_ip_str, WiFi.localIP().toString().c_str(), sizeof(_ota_ip_str) - 1);
        dbg_printf("OTA: using existing WiFi — %s\n", _ota_ip_str);
    }

    static const char* ota_headers[] = { "Content-Length" };
    httpServer.collectHeaders(ota_headers, 1);

    httpServer.on("/",         HTTP_GET,  handleOtaRoot);
    httpServer.on("/update",   HTTP_GET,  handleOtaRoot);
    httpServer.on("/update",   HTTP_POST, handleOtaUploadDone, handleOtaUploadData);
    httpServer.on("/releases", HTTP_GET,  handleGetReleases);
    httpServer.onNotFound([]() { httpServer.send(404, "text/plain", "Not found"); });
    httpServer.begin();

    _ota_server_active = true;
    dbg_println("OTA: STA server started — open fluiddial.local in browser");
}

void wifi_ota_force_ap_setup() {
    if (_ota_started_sta) {
        WiFi.disconnect(true);
        _ota_started_sta = false;
    }
    _ota_last_sta         = WL_IDLE_STATUS;
    _ota_connect_start_ms = 0;
    start_ota_ap_credentials();
    request_redisplay();
}

void wifi_stop_ota_server() {
    if (!_ota_server_active) return;
    httpServer.stop();
    dnsServer.stop();
    if (_ota_started_ap) {
        WiFi.softAPdisconnect(true);
        _ap_mode        = false;
        _ota_started_ap = false;
    }
    if (_ota_started_sta) {
        WiFi.disconnect(true);
        _ota_started_sta = false;
    }
    _ota_server_active = false;
    _ota_ap_mode       = false;
    _ota_progress      = 0;
    dbg_println("OTA: server stopped");
}

bool wifi_ota_server_active() { return _ota_server_active; }
bool wifi_ota_ap_mode()       { return _ota_ap_mode; }
bool wifi_ota_sta_connected() { return _ota_server_active && !_ota_ap_mode && (WiFi.status() == WL_CONNECTED); }
int  wifi_ota_progress()      { return _ota_progress; }
const char* wifi_ota_ip()     { return _ota_ip_str; }
const char* wifi_ota_error()  { return _ota_error_msg; }

#endif  // ARDUINO
