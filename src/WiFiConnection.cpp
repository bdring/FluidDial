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

#include <Esp.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// ─── Configuration ───────────────────────────────────────────────────────────

#define WIFI_AP_SSID       "FluidDial"
#define WIFI_AP_PASS       ""          // Open AP — no password needed
#define PREF_NAMESPACE     "fluidwifi"
#define WS_SUBPROTOCOL     "arduino"
#define RX_BUF_SIZE        2048
#define TX_BUF_SIZE        512
#define PING_INTERVAL_MS   10000       // Application-level PING every 10 s
#define STATUS_POLL_MS     500         // Send '?' every 500 ms while connected

// ─── Globals ─────────────────────────────────────────────────────────────────

static WebSocketsClient webSocket;
static WebServer        httpServer(80);
static DNSServer        dnsServer;

static bool _ap_mode       = false;
static bool _ws_connected  = false;
static bool _wifi_was_connected = false;

// Ring buffer for characters received from FluidNC via WebSocket.
// Written in the WS callback, read by fnc_getchar() (both on the main task).
static uint8_t _rx_buf[RX_BUF_SIZE];
static int     _rx_head = 0;
static int     _rx_tail = 0;

// Line buffer for outgoing text (GCode / $ commands).
static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;

// Timers.
static uint32_t _last_ping_ms   = 0;
static uint32_t _last_status_ms = 0;
static uint16_t _page_id        = 1234;

static void log_wifi_heap(const char* stage) {
    dbg_printf("%s free heap: %u\n", stage, ESP.getFreeHeap());
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

// ─── GrblParserC interface ────────────────────────────────────────────────────

// Called by GrblParserC to send one byte to FluidNC.
extern "C" void fnc_putchar(uint8_t c) {
    // Skip UART XON/XOFF flow-control bytes (irrelevant over WebSocket).
    if (c == 0x11 || c == 0x13) return;

    // Extended single-byte realtime commands: Ctrl-X and 0x80–0x9F.
    if (c == 0x18 || (c >= 0x80 && c <= 0x9F)) {
        if (_ws_connected) webSocket.sendBIN(&c, 1);
        return;
    }

    // ASCII realtime commands ('?', '!', '~') sent *outside* a text line.
    // GrblParserC calls fnc_putchar() with these as standalone bytes.
    if (_tx_len == 0 && (c == '?' || c == '!' || c == '~')) {
        if (_ws_connected) webSocket.sendBIN(&c, 1);
        return;
    }

    // Everything else: buffer until '\n', then send as a text frame.
    _tx_buf[_tx_len++] = c;
    if (c == '\n' || _tx_len >= TX_BUF_SIZE - 1) {
        if (_ws_connected) {
            webSocket.sendTXT((char*)_tx_buf, _tx_len);
        }
        _tx_len = 0;
    }
}

// Called by GrblParserC to receive one byte from FluidNC.
extern "C" int fnc_getchar() {
    return rx_pop();
}

// ─── WebSocket event handler ──────────────────────────────────────────────────

static void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            _ws_connected = true;
            dbg_println("WS: connected to FluidNC");

            // Replay startup log so GrblParserC parses the initial state.
            webSocket.sendTXT("$SS\n");
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

        case WStype_PING:
        case WStype_PONG:
        case WStype_ERROR:
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
  input{width:100%;padding:10px;border-radius:6px;border:1px solid #555;
        background:#2a2a2a;color:#eee;font-size:16px}
  input:focus{outline:none;border-color:#4CAF50}
  button{margin-top:24px;width:100%;padding:14px;background:#4CAF50;color:#fff;
         border:none;border-radius:6px;font-size:18px;cursor:pointer;font-weight:bold}
  button:hover{background:#45a049}
  .note{margin-top:16px;font-size:13px;color:#888;text-align:center}
</style>
</head>
<body>
<h2>FluidDial WiFi Setup</h2>
<p class="sub">Connect the FluidDial pendant to your WiFi network and FluidNC machine.</p>
<form method="POST" action="/save">
  <label>WiFi Network Name (SSID)</label>
  <input type="text" name="ssid" placeholder="YourNetworkName" autocomplete="off" required>
  <label>WiFi Password</label>
  <input type="password" name="pass" placeholder="Leave blank for open networks">
  <label>FluidNC IP Address</label>
  <input type="text" name="ip" placeholder="192.168.1.100"
         pattern="^(\d{1,3}\.){3}\d{1,3}$" required>
  <button type="submit">Save &amp; Connect</button>
</form>
<p class="note">The pendant will restart and connect automatically.</p>
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
    httpServer.send(200, "text/html", SETUP_HTML);
}

static void handleSave() {
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    String ip   = httpServer.arg("ip");

    if (ssid.length() == 0 || ip.length() == 0) {
        httpServer.send(400, "text/plain", "SSID and IP are required");
        return;
    }

    wifi_save_config(ssid.c_str(), pass.c_str(), ip.c_str());
    httpServer.send(200, "text/html", SAVED_HTML);

    delay(2000);
    ESP.restart();
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
    Preferences prefs;
    // Open read-write so the namespace is created on first boot, avoiding
    // the NVS_NOT_FOUND error that occurs with read-only on a fresh device.
    prefs.begin(PREF_NAMESPACE, false);
    String ssid = prefs.isKey("ssid") ? prefs.getString("ssid", "") : "";
    String pass = prefs.isKey("pass") ? prefs.getString("pass", "") : "";
    String ip   = prefs.isKey("ip")   ? prefs.getString("ip",   "") : "";
    prefs.end();

    if (ssid.length() > 0 && ip.length() > 0) {
        strncpy(cfg.ssid,      ssid.c_str(), sizeof(cfg.ssid)      - 1);
        strncpy(cfg.password,  pass.c_str(), sizeof(cfg.password)  - 1);
        strncpy(cfg.fluidnc_ip, ip.c_str(), sizeof(cfg.fluidnc_ip) - 1);
        cfg.valid = true;
    }
    return cfg;
}

void wifi_start_ap_setup() {
    _ap_mode      = true;
    _ws_connected = false;

    log_wifi_heap("Starting AP setup");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, (strlen(WIFI_AP_PASS) ? WIFI_AP_PASS : nullptr));

    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

    // DNS: redirect every domain to the AP gateway (captive portal).
    dnsServer.start(53, "*", apIP);

    httpServer.on("/",      HTTP_GET,  handleRoot);
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
    if (_ap_mode)               return "AP Setup Mode";
    if (!wifi_is_connected())   return "Connecting WiFi…";
    if (!_ws_connected)         return "Connecting FluidNC…";
    return "Connected";
}

void wifi_init() {
    // Random page ID (matches WebUI behaviour).
    _page_id = (uint16_t)(random(1000, 9999));
    log_wifi_heap("Before WiFi init");

    WiFiConfig cfg = wifi_load_config();

    if (!cfg.valid) {
        dbg_println("No WiFi config found — starting AP setup mode");
        wifi_start_ap_setup();
        return;
    }

    dbg_printf("Connecting to WiFi: %s\n", cfg.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.ssid, cfg.password[0] ? cfg.password : nullptr);

    // Configure WebSocket client — it will connect once WiFi is up.
    webSocket.begin(cfg.fluidnc_ip, 80, "/", WS_SUBPROTOCOL);
    webSocket.onEvent(onWsEvent);
    webSocket.setReconnectInterval(3000);  // retry every 3 s on disconnect
}

void wifi_poll() {
    if (_ap_mode) {
        dnsServer.processNextRequest();
        httpServer.handleClient();
        return;
    }

    // Drive the WebSocket state machine.
    webSocket.loop();

    // Detect WiFi reconnects so the WebSocket can recover.
    bool now_connected = wifi_is_connected();
    if (now_connected && !_wifi_was_connected) {
        dbg_printf("WiFi connected — IP: %s\n",
                   WiFi.localIP().toString().c_str());
    }
    if (!now_connected && _wifi_was_connected) {
        _ws_connected = false;
        set_disconnected_state();
        dbg_println("WiFi lost");
    }
    _wifi_was_connected = now_connected;

    if (!_ws_connected) return;

    uint32_t now = millis();

    // Application-level PING every 10 s (keeps FluidNC page-tracking happy).
    if (now - _last_ping_ms >= PING_INTERVAL_MS) {
        _last_ping_ms = now;
        char ping[32];
        snprintf(ping, sizeof(ping), "PING:%d\n", _page_id);
        webSocket.sendTXT(ping);
    }

    // Explicit status poll every 500 ms.
    // (FluidNC auto-report via $RI is also set when state transitions from
    // Disconnected in show_state(), but we poll here as a belt-and-suspenders
    // measure until the first status arrives.)
    if (now - _last_status_ms >= STATUS_POLL_MS) {
        _last_status_ms = now;
        uint8_t qmark = '?';
        webSocket.sendBIN(&qmark, 1);
    }
}

// Stub: flow control is a no-op over WebSocket.
void resetFlowControl() {}

#endif  // ARDUINO
