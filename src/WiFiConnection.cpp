// 2026 - Figamore
// Use of this source code is governed by a GPLv3 license.
//
// WiFi / WebSocket transport layer for FluidDial.
//
// Provides fnc_putchar() and fnc_getchar() over a WebSocket connection to
// FluidNC instead of UART.

#ifdef ARDUINO

#include "WiFiConnection.h"
#include "FluidNCModel.h"
#include "System.h"
#include "Scene.h"   // current_scene->reDisplay()

#include <Esp.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebSocketsClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ─── Configuration ───────────────────────────────────────────────────────────

#define WIFI_AP_SSID       "FluidDial"
#define WIFI_AP_PASS       ""          // Open AP — no password needed
#define PREF_NAMESPACE     "fluidwifi"
#define WS_SUBPROTOCOL     "arduino"
#define FLUIDNC_WS_PORT    80
#define FLUIDNC_WS_PATH    "/"
#define HARDCODE_TEST_WIFI 0
#define TEST_WIFI_SSID     "FluidNC"
#define TEST_WIFI_PASS     "12345678"
#define TEST_FLUIDNC_IP    "192.168.0.1"
#define RX_BUF_SIZE        2048
#define TX_BUF_SIZE        512
#define STATUS_POLL_MS     500         // Send '?' every 500 ms while connected
#define WIFI_RETRY_DELAY_MS 15000     // Retry WiFi.begin() this long after a failure

// ─── Globals ─────────────────────────────────────────────────────────────────

static WebSocketsClient webSocket;
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

// Ring buffer for characters received from FluidNC via WebSocket.
// Written in the WS callback, read by fnc_getchar() (both on the main task).
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

static bool is_dotted_decimal(const char* s) {
    for (const char* p = s; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || *p == '.')) return false;
    }
    return true;
}

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length);

static void dnsResolveTask(void* /*param*/) {
    IPAddress ip;
    // hostByName() blocks until the mDNS/DNS reply arrives or the stack times
    // out — running it on a background task keeps the main loop responsive.
    bool ok = (WiFi.hostByName(_active_cfg.fluidnc_ip, ip) == 1);
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
    xTaskCreate(dnsResolveTask, "dns_resolve", 4096, nullptr, 1, nullptr);
}

static void ws_begin(const char* host) {
    dbg_printf("Starting WebSocket: ws://%s:%d%s\n", host, FLUIDNC_WS_PORT, FLUIDNC_WS_PATH);
    webSocket.begin(host, FLUIDNC_WS_PORT, FLUIDNC_WS_PATH, WS_SUBPROTOCOL);
    webSocket.onEvent(onWsEvent);
    webSocket.setReconnectInterval(3000);
    webSocket.enableHeartbeat(15000, 3000, 2);
    _ws_started = true;
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

static bool ws_send_text(uint8_t* payload, size_t length) {
    return webSocket.sendTXT(payload, length);
}

static bool ws_send_bin(const uint8_t* payload, size_t length) {
    return webSocket.sendBIN(payload, length);
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

// ─── WebSocket transport primitives ──────────────────────────────────────────
// These are called by fnc_putchar/fnc_getchar (defined in SystemArduino.cpp)
// when the active transport is WiFi.

// Send one byte to FluidNC via WebSocket.
void ws_putchar(uint8_t c) {
    // Skip UART XON/XOFF flow-control bytes (irrelevant over WebSocket).
    if (c == 0x11 || c == 0x13) return;

    // Extended single-byte realtime commands: Ctrl-X, 0x80–0x9F, and the
    // IO-extender commands 0xB0–0xB3 (ACK=0xB2, NAK=0xB3).  FluidNC uses
    // ACK/NAK flow control when streaming JSON file listings, even over
    // WebSocket — without it the first chunk arrives and FluidNC stalls
    // waiting for acknowledgement, leaving the file list empty.
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

// Receive one byte from the WebSocket ring buffer (-1 if empty).
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

// ─── WebSocket event handler ──────────────────────────────────────────────────

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            _ws_connected = true;
            _last_status_ms = 0;
            dbg_printf("WS: connected to FluidNC at ws://%s:%d%s\n", _active_cfg.fluidnc_ip, FLUIDNC_WS_PORT, FLUIDNC_WS_PATH);
            // Update the badge immediately; state report from FluidNC will follow shortly.
            current_scene->reDisplay();
            break;
        }

        case WStype_DISCONNECTED:
            _ws_connected = false;
            dbg_println("WS: disconnected from FluidNC");
            set_disconnected_state();
            break;

        case WStype_TEXT:
        case WStype_BIN: {
            // Push every received byte into the ring buffer.
            // GrblParserC's fnc_getchar() will drain it character-by-character.
            for (size_t i = 0; i < length; i++) {
                if (payload[i] == '\r') continue;  // Strip CR
                rx_push(payload[i]);
            }
            // Ensure the last line is newline-terminated.
            if (length > 0 && payload[length - 1] != '\n') {
                rx_push('\n');
            }
            update_rx_time();
            break;
        }

        case WStype_ERROR:
            dbg_println("WS: error");
            break;

        case WStype_PING:
        case WStype_PONG:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
    }
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

static void handleRoot() {
    WiFiConfig cfg = wifi_load_config();
    String page = SETUP_HTML;
    page.replace("%SSID_VAL%", cfg.valid ? String(cfg.ssid) : "");
    page.replace("%IP_VAL%",   cfg.valid ? String(cfg.fluidnc_ip) : "");
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
    _ws_connected       = false;
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
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

bool websocket_is_connected() {
    return _ws_connected;
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

void wifi_init() {
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
        // No credentials saved yet — auto-start AP so the user can configure
        // via browser immediately (captive portal at 192.168.4.1).
        dbg_println("No WiFi credentials — starting AP setup mode automatically");
        wifi_start_ap_setup();
        current_scene->reDisplay();  // switch scene from "not configured" to AP view
        return;
    }

    _wifi_stack_started = true;
    dbg_printf("Connecting to WiFi: %s  FluidNC: %s\n", cfg.ssid, cfg.fluidnc_ip);
    _active_cfg              = cfg;
    _ws_started              = false;
    _ws_connected            = false;
    _wifi_was_connected      = false;
    _last_wifi_status        = WL_IDLE_STATUS;
    _wifi_error_msg          = nullptr;
    _wifi_retry_at           = 0;
    _wifi_disconnect_reason  = 0;
    _wifi_ever_connected     = false;
    _wifi_connect_start_ms   = 0;  // set after WiFi.begin() below
    _handshake_timeout_count = 0;

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
        current_scene->reDisplay();
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
                if (changed) current_scene->reDisplay();
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

    // Detect WiFi reconnects so the WebSocket can recover.
    bool now_connected = wifi_status == WL_CONNECTED;
    if (now_connected && !_wifi_was_connected) {
        dbg_printf("WiFi connected — IP: %s\n",
                   WiFi.localIP().toString().c_str());
        // Initialise mDNS so ".local" names resolve via the lwIP mDNS resolver.
        if (!MDNS.begin("fluiddial")) {
            dbg_println("mDNS init failed — .local hostnames may not resolve");
        }
        if (is_dotted_decimal(_active_cfg.fluidnc_ip)) {
            // Plain IP address — start WebSocket immediately.
            ws_begin(_active_cfg.fluidnc_ip);
        } else if (!_dns_resolving) {
            // Hostname — resolve asynchronously; ws_begin() runs when done.
            start_dns_resolve();
        }
        // WiFi just came up — update badge from "WiFi..." to "WiFi OK".
        current_scene->reDisplay();
    }
    if (!now_connected && _wifi_was_connected) {
        if (_ws_started) {
            webSocket.disconnect();
            _ws_started = false;
        }
        _dns_done     = false;  // discard any in-flight DNS result
        _ws_connected = false;
        set_disconnected_state();
        dbg_println("WiFi lost");
    }
    _wifi_was_connected = now_connected;

    // Handle async DNS completion (hostname -> IP resolution).
    // Only act if WiFi is still up and the WebSocket hasn't already started.
    if (_dns_done && !_ws_started && now_connected) {
        bool ok = _dns_ok;
        _dns_done = false;
        if (ok) {
            dbg_printf("Hostname resolved: %s -> %s\n",
                       _active_cfg.fluidnc_ip, _dns_result_str);
            ws_begin(_dns_result_str);
        } else {
            dbg_printf("Hostname resolution failed: %s\n", _active_cfg.fluidnc_ip);
            _wifi_error_msg = "Host not found";
        }
        current_scene->reDisplay();
    }

    if (_ws_started) {
        // Drive the WebSocket state machine only after WiFi is up.
        webSocket.loop();
    }

    // Animate the connecting badge until FluidNC is fully connected.
    if (!_ws_connected) {
        static uint32_t _last_anim_ms = 0;
        uint32_t        now_ms        = millis();
        if (now_ms - _last_anim_ms >= 400) {
            _last_anim_ms = now_ms;
            current_scene->reDisplay();
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
