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
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

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

// ─── Globals ─────────────────────────────────────────────────────────────────

static int              _sock = -1;
static uint32_t         _tcp_next_try_ms = 0;
static char             _fluidnc_remote_ip[40] = {};  // dotted-decimal, cached for reconnects
static WebServer        httpServer(80);
static DNSServer        dnsServer;

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
    BaseType_t task_created = xTaskCreate(dnsResolveTask, "dns_resolve", 4096, nullptr, 1, nullptr);
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
    struct timeval snd_tv = { 2, 0 };
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

// Open a raw TCP socket to FluidNC's telnet server (port 23). Synchronous;
// returns true on success. Called from wifi_poll() once WiFi STA is up and
// the FluidNC IP has been resolved.
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
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(FLUIDNC_TELNET_PORT);
    dest.sin_addr.s_addr = (uint32_t)ip;
    if (::connect(fd, (struct sockaddr*)&dest, sizeof(dest)) != 0) {
        dbg_printf("Telnet: connect errno=%d\n", errno);
        ::close(fd);
        return false;
    }
    tcp_apply_opts(fd);
    _sock = fd;
    _ws_connected   = true;
    _last_status_ms = 0;
    dbg_printf("Telnet: connected to %s:%d (fd=%d)\n", host, FLUIDNC_TELNET_PORT, fd);
    request_redisplay();
    return true;
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

// Bulk send over the raw socket. Blocks up to SO_SNDTIMEO per chunk;
// transient EAGAIN drops the remainder of this flush but keeps the
// socket so the in-flight document survives. Any other errno tears the
// socket down so wifi_poll() can reconnect.
static bool tcp_send_all(const uint8_t* payload, size_t length) {
    if (_sock < 0 || length == 0) {
        return _sock >= 0;
    }
    size_t off = 0;
    while (off < length) {
        ssize_t n = ::send(_sock, payload + off, length - off, 0);
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
    return tcp_send_all(payload, length);
}

static bool ws_send_bin(const uint8_t* payload, size_t length) {
    return tcp_send_all(payload, length);
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

// ─── Runtime transport mode ───────────────────────────────────────────────────

static int _uart_mode_cached = -1;  // -1 = not yet read from NVS

bool wifi_use_uart_mode() {
    if (_uart_mode_cached < 0) {
        Preferences prefs;
        prefs.begin(PREF_NAMESPACE, false);  // read-write ensures namespace exists
        _uart_mode_cached = prefs.getBool("uart_mode", false) ? 1 : 0;
        prefs.end();
    }
    return _uart_mode_cached == 1;
}

void wifi_set_uart_mode(bool uart) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putBool("uart_mode",   uart);
    prefs.putBool("setup_done",  true);  // clears first-boot flag
    prefs.end();
    _uart_mode_cached = uart ? 1 : 0;
    dbg_printf("Transport mode set to: %s\n", uart ? "UART" : "WiFi");
}

bool wifi_is_first_boot() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    bool done = prefs.getBool("setup_done", false);
    prefs.end();
    return !done;
}

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
  fetch('/scan').then(function(r){return r.json();}).then(function(nets){
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
    st.textContent='Scan failed. Try again.';
    btn.disabled=false; btn.textContent='Scan';
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
    WiFiConfig cfg = wifi_load_config();
    String page = SETUP_HTML;
    page.replace("%SSID_VAL%", cfg.valid ? htmlEscape(String(cfg.ssid)) : "");
    page.replace("%IP_VAL%",   cfg.valid ? htmlEscape(String(cfg.fluidnc_ip)) : "");
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
    int n = WiFi.scanNetworks(false, false);  // blocking, no hidden networks

    String json = "[";
    for (int i = 0; i < n && i < 32; i++) {
        if (i > 0) json += ",";
        // Escape backslashes and double-quotes to produce valid JSON.
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
    // Redirect everything to the setup page (captive portal behaviour).
    httpServer.sendHeader("Location", "http://192.168.4.1/", true);
    httpServer.send(302, "text/plain", "");
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

    if (ssid.length() > 0 && ip.length() > 0) {
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

bool wifi_in_ap_mode() {
    return _ap_mode;
}

const char* wifi_ap_ssid() {
    return WIFI_AP_SSID;
}

const char* wifi_status_str() {
    if (wifi_use_uart_mode())   return "UART Mode";
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

void wifi_init(bool auto_ap) {
    if (wifi_use_uart_mode()) {
        dbg_println("Transport: UART mode — WiFi stack not started");
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

    static bool _event_registered = false;
    if (!_event_registered) {
        WiFi.onEvent(onWiFiDisconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        _event_registered = true;
    }

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
    if (!_wifi_stack_started) return;  // wifi_init() never ran (first boot or UART mode)
    if (wifi_use_uart_mode()) return;

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
        if (!MDNS.begin("fluiddial")) {
            dbg_println("mDNS init failed — .local hostnames may not resolve");
        }
        if (is_dotted_decimal(_active_cfg.fluidnc_ip)) {
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
        bool ok = _dns_ok;
        _dns_done = false;
        if (ok) {
            dbg_printf("Hostname resolved: %s -> %s\n",
                       _active_cfg.fluidnc_ip, _dns_result_str);
            tcp_begin(_dns_result_str);
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
        if (_sock >= 0) {
            tcp_refill_rx();
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

#endif  // ARDUINO
