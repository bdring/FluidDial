// 2026 - Figamore

#ifdef ARDUINO
#ifdef USE_WIFI

#include "PeerLink.h"
#include "FluidNCModel.h"
#include "System.h"
#include "Scene.h"

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <stdio.h>

#define PREF_NAMESPACE       "fluidwifi"
#define ESPNOW_PAIR_CHANNEL  1
#define BEACON_INTERVAL_MS   2000
#define CODE_LIFETIME_MS     60000
#define CONNECT_TIMEOUT_MS   5000
#define KEEPALIVE_INTERVAL_MS 2000
#define RECONNECT_PROBE_MS   1000
#define RX_BUF_SIZE          2048
#define TX_BUF_SIZE          512
#define MAX_FRAGS            8
#define FRAG_HEADER_SIZE     4
#define FRAG_PAYLOAD_MAX     (250 - FRAG_HEADER_SIZE)  // 246 bytes

#define PKT_DISCOVERY  0x01
#define PKT_PAIR_ACK   0x02
#define PKT_DATA       0x03
#define PKT_REALTIME   0x05
#define PKT_KEEPALIVE  0x06

static const uint8_t PROBE_ORDER[13] = {6, 11, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};


struct __attribute__((packed)) DiscoveryPkt {
    uint8_t  type;          // PKT_DISCOVERY
    uint8_t  mac[6];        // FluidDial MAC
    uint8_t  code_hash[4];  // SHA-256(code)[0:4]
    uint8_t  channel;       // channel FluidDial is currntly using
};

struct __attribute__((packed)) PairAckPkt {
    uint8_t  type;       // PKT_PAIR_ACK
    uint8_t  mac[6];     // FluidNC MAC
    uint8_t  channel;    // FluidNC's operational WiFi channel
    uint32_t timestamp;  // millis() on FluidNC (anti-replay)
};

struct __attribute__((packed)) FragHeader {
    uint8_t type;        // PKT_DATA
    uint8_t seq;
    uint8_t frag_idx;
    uint8_t total_frags;
};


static uint8_t  _peer_mac[6] = {};
static uint8_t  _lmk[16]     = {};
static uint8_t  _op_channel  = ESPNOW_PAIR_CHANNEL;
static bool     _is_paired   = false;


static bool     _is_connected      = false;
static uint32_t _last_rx_ms        = 0;
static uint32_t _keepalive_last_ms = 0;
static volatile int8_t _peer_rssi  = 0;

static bool     _pairing_active   = false;
static bool     _pairing_complete = false;
static char     _pairing_code[7]  = {};
static uint8_t  _pairing_lmk[16] = {};
static uint32_t _beacon_last_ms   = 0;
static uint32_t _code_start_ms    = 0;
static bool     _probe_active     = false;
static uint8_t  _probe_idx        = 0;

static bool     _reconnect_active    = false;
static uint8_t  _reconnect_probe_idx = 0;
static uint32_t _reconnect_beacon_ms = 0;

static uint8_t  _rx_buf[RX_BUF_SIZE];
static volatile int _rx_head = 0;
static int          _rx_tail = 0;

// TX line buffer
static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;
static uint8_t _tx_seq = 0;

// Fragment reassembly (recv callback only)
static uint8_t  _frag_buf[MAX_FRAGS][FRAG_PAYLOAD_MAX];
static uint8_t  _frag_len[MAX_FRAGS];
static uint8_t  _frag_got;
static uint8_t  _frag_total;
static uint8_t  _frag_seq;
static bool     _frag_pending;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void derive_lmk(const char* code, uint8_t out_lmk[16]) {
    const char* prefix = "fluiddial-espnow:";
    uint8_t     input[32];
    size_t      prefix_len = strlen(prefix);
    size_t      code_len   = strlen(code);
    memcpy(input, prefix, prefix_len);
    memcpy(input + prefix_len, code, code_len);
    uint8_t hash[32];
    mbedtls_sha256(input, prefix_len + code_len, hash, 0);
    memcpy(out_lmk, hash, 16);
}

static void derive_device_code(char out[7]) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    const char* salt = "fluiddial-code-v1:";
    uint8_t     hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)salt, strlen(salt));
    mbedtls_sha256_update(&ctx, mac, 6);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    uint32_t n = ((uint32_t)hash[0] << 24 | (uint32_t)hash[1] << 16 |
                  (uint32_t)hash[2] << 8  | (uint32_t)hash[3]) % 1000000u;
    snprintf(out, 7, "%06u", n);
}


static inline void rx_push(uint8_t c) {
    int next = (_rx_head + 1) % RX_BUF_SIZE;
    if (next != _rx_tail) {
        _rx_buf[_rx_head] = c;
        _rx_head          = next;
    }
}

static inline int rx_pop() {
    int head = _rx_head;
    if (head == _rx_tail) return -1;
    uint8_t c = _rx_buf[_rx_tail];
    _rx_tail  = (_rx_tail + 1) % RX_BUF_SIZE;
    return (unsigned char)c;
}


static void register_peer(const uint8_t mac[6], uint8_t channel, bool encrypt, const uint8_t lmk[16]) {
    if (esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t p = {};
        esp_now_get_peer(mac, &p);
        p.channel = channel;
        p.encrypt = encrypt;
        if (encrypt && lmk) memcpy(p.lmk, lmk, 16);
        esp_now_mod_peer(&p);
    } else {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, mac, 6);
        p.channel = channel;
        p.encrypt = encrypt;
        if (encrypt && lmk) memcpy(p.lmk, lmk, 16);
        p.ifidx   = WIFI_IF_STA;
        esp_now_add_peer(&p);
    }
}


#if ESP_IDF_VERSION_MAJOR >= 5
static void on_data_recv(const esp_now_recv_info_t* recv_info,
                         const uint8_t* data, int len) {
    if (_is_paired && recv_info && recv_info->rx_ctrl) {
        _peer_rssi = (int8_t)recv_info->rx_ctrl->rssi;
    }
#else
static void on_data_recv(const uint8_t* /* mac_addr */,
                         const uint8_t* data, int len) {
#endif
    if (len < 1) return;
    uint8_t pkt_type = data[0];

    if (pkt_type == PKT_PAIR_ACK && _pairing_active
        && len >= (int)sizeof(PairAckPkt)) {
        const PairAckPkt* ack = (const PairAckPkt*)data;

        memcpy(_peer_mac, ack->mac, 6);
        memcpy(_lmk, _pairing_lmk, 16);
        _op_channel = (ack->channel > 0 && ack->channel <= 14)
                      ? ack->channel : ESPNOW_PAIR_CHANNEL;

        register_peer(_peer_mac, _op_channel, true, _lmk);
        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

        Preferences prefs;
        prefs.begin(PREF_NAMESPACE, false);
        prefs.putBytes("espnow_mac", _peer_mac, 6);
        prefs.putBytes("espnow_lmk", _lmk, 16);
        prefs.putUChar("espnow_ch",  _op_channel);
        prefs.end();

        _is_paired        = true;
        _pairing_complete = true;
        _pairing_active   = false;
        _probe_active     = false;
        _last_rx_ms       = millis();
        _is_connected     = true;

        dbg_printf("ESP-NOW: pairing complete — peer %02x:%02x:%02x:%02x:%02x:%02x ch=%d\n",
                   _peer_mac[0], _peer_mac[1], _peer_mac[2],
                   _peer_mac[3], _peer_mac[4], _peer_mac[5], _op_channel);
        return;
    }

    if (!_is_paired) return;

    _last_rx_ms   = millis();
    _is_connected = true;

    if (pkt_type == PKT_KEEPALIVE) {
        if (len >= 2) {
            _peer_rssi = (int8_t)data[1];
        }
        return;
    }

    if (pkt_type == PKT_REALTIME && len >= 2) {
        rx_push(data[1]);
        update_rx_time();
        return;
    }

    // DATA FRAG
    if (pkt_type == PKT_DATA && len >= FRAG_HEADER_SIZE) {
        const FragHeader* hdr  = (const FragHeader*)data;
        uint8_t           idx   = hdr->frag_idx;
        uint8_t           total = hdr->total_frags;
        uint8_t           seq   = hdr->seq;
        int               plen  = len - FRAG_HEADER_SIZE;

        if (total == 0 || total > MAX_FRAGS || idx >= total || plen < 0
            || plen > FRAG_PAYLOAD_MAX) return;

        if (!_frag_pending || _frag_seq != seq) {
            _frag_got     = 0;
            _frag_total   = total;
            _frag_seq     = seq;
            _frag_pending = true;
            memset(_frag_len, 0, sizeof(_frag_len));
        }

        memcpy(_frag_buf[idx], data + FRAG_HEADER_SIZE, plen);
        _frag_len[idx] = (uint8_t)plen;
        _frag_got     |= (uint8_t)(1u << idx);

        uint8_t full_mask = (total >= 8) ? 0xFF : (uint8_t)((1u << total) - 1u);
        if ((_frag_got & full_mask) != full_mask) return;

        for (int i = 0; i < total; i++) {
            for (int j = 0; j < _frag_len[i]; j++) {
                uint8_t c = _frag_buf[i][j];
                if (c == '\r') continue;
                rx_push(c);
            }
        }
        if (_frag_len[total - 1] == 0
            || _frag_buf[total - 1][_frag_len[total - 1] - 1] != '\n') {
            rx_push('\n');
        }
        _frag_pending = false;
        update_rx_time();
    }
}

static void on_data_sent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}


static void send_fragments(const uint8_t* data, size_t len) {
    if (!_is_paired || len == 0) return;

    uint8_t total = (uint8_t)((len + FRAG_PAYLOAD_MAX - 1) / FRAG_PAYLOAD_MAX);
    if (total == 0) total = 1;
    if (total > MAX_FRAGS) total = MAX_FRAGS;

    uint8_t pkt[250];
    size_t  offset = 0;

    for (uint8_t i = 0; i < total; i++) {
        size_t chunk = len - offset;
        if (chunk > FRAG_PAYLOAD_MAX) chunk = FRAG_PAYLOAD_MAX;

        FragHeader* hdr  = (FragHeader*)pkt;
        hdr->type        = PKT_DATA;
        hdr->seq         = _tx_seq;
        hdr->frag_idx    = i;
        hdr->total_frags = total;
        memcpy(pkt + FRAG_HEADER_SIZE, data + offset, chunk);

        esp_now_send(_peer_mac, pkt, FRAG_HEADER_SIZE + chunk);
        offset += chunk;
    }
    _tx_seq++;
}


void espnow_init() {
    dbg_println("ESP-NOW: initialising");

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    if (esp_now_init() != ESP_OK) {
        dbg_println("ESP-NOW: init failed");
        return;
    }

    {
        const char* pmk_seed = "fluiddial-espnow-pmk-v1";
        uint8_t     hash[32];
        mbedtls_sha256((const uint8_t*)pmk_seed, strlen(pmk_seed), hash, 0);
        esp_now_set_pmk(hash);
    }

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    bool has_mac = prefs.isKey("espnow_mac");
    bool has_lmk = prefs.isKey("espnow_lmk");
    if (has_mac && has_lmk) {
        prefs.getBytes("espnow_mac", _peer_mac, 6);
        prefs.getBytes("espnow_lmk", _lmk, 16);
        _op_channel = prefs.getUChar("espnow_ch", ESPNOW_PAIR_CHANNEL);
        prefs.end();

        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
        register_peer(_peer_mac, _op_channel, true, _lmk);
        _is_paired = true;
        dbg_printf("ESP-NOW: loaded pairing, ch=%d\n", _op_channel);
    } else {
        prefs.end();
        dbg_println("ESP-NOW: no saved pairing");
    }

    derive_device_code(_pairing_code);
    dbg_printf("ESP-NOW: device pairing code = %s\n", _pairing_code);
}

void espnow_poll() {
    uint32_t now = millis();

    if (_is_connected && (now - _last_rx_ms) > CONNECT_TIMEOUT_MS) {
        _is_connected = false;
        set_disconnected_state();
    }

    if (_is_paired && !_is_connected) {
        if (!_reconnect_active) {
            _reconnect_active    = true;
            _reconnect_probe_idx = 0;
            _reconnect_beacon_ms = 0;
            if (current_scene) current_scene->reDisplay();
        }

        if ((now - _reconnect_beacon_ms) >= RECONNECT_PROBE_MS) {
            _reconnect_beacon_ms = now;

            uint8_t ch = PROBE_ORDER[_reconnect_probe_idx % 13];
            _reconnect_probe_idx = (_reconnect_probe_idx + 1) % 13;

            _op_channel = ch;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            register_peer(_peer_mac, ch, true, _lmk);

            uint8_t pkt[1] = {PKT_KEEPALIVE};
            esp_now_send(_peer_mac, pkt, 1);
            dbg_printf("ESP-NOW: reconnect probe ch=%d\n", ch);
        }
    } else if (_reconnect_active && _is_connected) {
        _reconnect_active = false;
        Preferences prefs;
        prefs.begin(PREF_NAMESPACE, false);
        prefs.putUChar("espnow_ch", _op_channel);
        prefs.end();
        if (current_scene) current_scene->reDisplay();
        dbg_printf("ESP-NOW: reconnected on ch=%d (RSSI %d dBm)\n", _op_channel, _peer_rssi);
    }

    if (_is_paired && _is_connected && (now - _keepalive_last_ms) >= KEEPALIVE_INTERVAL_MS) {
        _keepalive_last_ms = now;
        uint8_t pkt[1] = {PKT_KEEPALIVE};
        esp_now_send(_peer_mac, pkt, 1);
    }

    if (!_pairing_active) return;

    if ((now - _code_start_ms) >= CODE_LIFETIME_MS) {
        _code_start_ms = now;
    }

    if ((now - _beacon_last_ms) >= BEACON_INTERVAL_MS) {
        _beacon_last_ms = now;

        if (_probe_active) {
            _probe_idx  = (_probe_idx + 1) % 13;
            _op_channel = PROBE_ORDER[_probe_idx];
            esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

            if (esp_now_is_peer_exist(BROADCAST_MAC)) {
                esp_now_del_peer(BROADCAST_MAC);
            }
            esp_now_peer_info_t bp = {};
            memcpy(bp.peer_addr, BROADCAST_MAC, 6);
            bp.channel = _op_channel;
            bp.encrypt = false;
            bp.ifidx   = WIFI_IF_STA;
            esp_now_add_peer(&bp);
        }

        uint8_t my_mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, my_mac);

        DiscoveryPkt pkt;
        pkt.type    = PKT_DISCOVERY;
        memcpy(pkt.mac, my_mac, 6);
        memcpy(pkt.code_hash, _pairing_lmk, 4);
        pkt.channel = _op_channel;

        esp_now_send(BROADCAST_MAC, (const uint8_t*)&pkt, sizeof(pkt));
        dbg_printf("ESP-NOW: beacon ch=%d code=%s\n", _op_channel, _pairing_code);
    }
}


void espnow_putchar(uint8_t c) {
    if (c == 0x11 || c == 0x13) return;

    if (c == 0x18 || (c >= 0x80 && c <= 0x9F) || (c >= 0xB0 && c <= 0xB3)) {
        if (_is_paired) {
            uint8_t pkt[2] = {PKT_REALTIME, c};
            esp_now_send(_peer_mac, pkt, 2);
        }
        return;
    }

    if (_tx_len == 0 && (c == '?' || c == '!' || c == '~')) {
        if (_is_paired) {
            uint8_t pkt[2] = {PKT_REALTIME, c};
            esp_now_send(_peer_mac, pkt, 2);
        }
        return;
    }

    _tx_buf[_tx_len++] = c;
    if (c == '\n' || _tx_len >= TX_BUF_SIZE - 1) {
        send_fragments(_tx_buf, _tx_len);
        _tx_len = 0;
    }
}

int espnow_getchar() {
    return rx_pop();
}


bool espnow_is_paired()       { return _is_paired; }
bool espnow_is_connected()    { return _is_paired && _is_connected; }
bool   espnow_is_reconnecting() { return _reconnect_active; }
int8_t espnow_rssi()            { return _peer_rssi; }

int espnow_signal_bars() {
    if (!_is_connected) return 0;
    return 4;
}

const char* espnow_status_str() {
    if (!_is_paired)         return "Not Paired";
    if (_reconnect_active)   return "Searching...";
    if (_is_connected)       return "Connected";
    return "Paired";
}


const char* espnow_start_pairing() {
    _probe_active = true;
    _probe_idx    = 0;
    _op_channel   = PROBE_ORDER[0];
    esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_del_peer(BROADCAST_MAC);
    }

    derive_lmk(_pairing_code, _pairing_lmk);

    _code_start_ms    = millis();
    _pairing_active   = true;
    _pairing_complete = false;
    _beacon_last_ms   = 0;

    dbg_printf("ESP-NOW: pairing session started, code=%s, priority ch order 1/6/11/...\n",
               _pairing_code);
    return _pairing_code;
}

void espnow_cancel_pairing() {
    _pairing_active = false;
    _probe_active   = false;
    dbg_println("ESP-NOW: pairing cancelled");
}

bool        espnow_pairing_complete() { return _pairing_complete; }
const char* espnow_pairing_code()     { return _pairing_code; }

uint32_t espnow_code_remaining_ms() {
    if (!_pairing_active) return 0;
    uint32_t elapsed = millis() - _code_start_ms;
    return (elapsed < CODE_LIFETIME_MS) ? (CODE_LIFETIME_MS - elapsed) : 0;
}

bool espnow_has_saved_pairing() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, true);
    bool has = prefs.isKey("espnow_mac") && prefs.isKey("espnow_lmk");
    prefs.end();
    return has;
}

void espnow_clear_pairing() {
    if (_is_paired && esp_now_is_peer_exist(_peer_mac)) {
        esp_now_del_peer(_peer_mac);
    }
    memset(_peer_mac, 0, 6);
    memset(_lmk, 0, 16);
    _is_paired       = false;
    _is_connected    = false;
    _reconnect_active = false;
    _peer_rssi       = 0;

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.remove("espnow_mac");
    prefs.remove("espnow_lmk");
    prefs.remove("espnow_ch");
    prefs.end();
    dbg_println("ESP-NOW: pairing cleared");
}

#endif
#endif
