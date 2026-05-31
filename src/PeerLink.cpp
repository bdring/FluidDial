// 2026 - Figamore

#ifdef ARDUINO
#ifdef USE_WIFI

#include "PeerLink.h"

#ifdef USE_ESPNOW

#include "FluidNCModel.h"
#include "System.h"
#include "Scene.h"

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_efuse.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <string.h>
#include <stdio.h>
#include <atomic>

#define PREF_NAMESPACE             "fluidespnow"
#define ESPNOW_PAIR_CHANNEL        1
#define BEACON_INTERVAL_MS         2000
#define CODE_LIFETIME_MS           60000
#define CONNECT_TIMEOUT_MS         5000
#define KEEPALIVE_INTERVAL_MS      2000
#define RECONNECT_PROBE_MS         1000
#define RX_BUF_SIZE                2048
#define TX_BUF_SIZE                512
#define MAX_FRAGS                  8
#define FRAG_HEADER_SIZE           4
#define FRAG_PAYLOAD_MAX           (250 - FRAG_HEADER_SIZE)  // 246 bytes
#define FRAG_REASSEMBLY_TIMEOUT_MS 3000   // discard stale partial reassembly

#define PKT_DISCOVERY  0x01
#define PKT_PAIR_ACK   0x02
#define PKT_DATA       0x03
#define PKT_REALTIME   0x05
#define PKT_KEEPALIVE  0x06

static const uint8_t PROBE_ORDER[13] = {6, 11, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};

struct __attribute__((packed)) DiscoveryPkt {
    uint8_t  type;          // PKT_DISCOVERY
    uint8_t  mac[6];        // FluidDial MAC
    uint8_t  code_hash[4];  // SHA-256(lmk)[0:4]
    uint8_t  channel;       // channel FluidDial is currntly using
};

struct __attribute__((packed)) PairAckPkt {
    uint8_t  type;       // PKT_PAIR_ACK
    uint8_t  mac[6];     // FluidNC MAC
    uint8_t  channel;    // FluidNC's operational WiFi channel
    uint32_t timestamp;  // millis() on FluidNC
};

struct __attribute__((packed)) FragHeader {
    uint8_t type;        // PKT_DATA
    uint8_t seq;
    uint8_t frag_idx;
    uint8_t total_frags;
};


static uint8_t          _peer_mac[6]  = {};
static uint8_t          _lmk[16]      = {};
static uint8_t          _op_channel   = ESPNOW_PAIR_CHANNEL;
static volatile bool    _is_paired    = false;

static volatile bool     _is_connected     = false;
static volatile uint32_t _last_rx_ms       = 0;
static          uint32_t _keepalive_last_ms = 0;
static volatile int8_t   _peer_rssi         = -100;  // no measurement yet

// EMA: a = 1/5
static void update_rssi(int8_t raw) {
    int8_t cur = _peer_rssi;
    _peer_rssi = (cur == -100) ? raw
                               : (int8_t)(((int)cur * 4 + (int)raw) / 5);
}

static volatile bool    _pairing_active   = false;
static volatile bool    _pairing_complete = false;
static std::atomic<bool> _pair_ack_pending {false};
static PairAckPkt       _pair_ack_pending_pkt = {};
static char             _pairing_code[9]  = {};
static uint8_t          _pairing_lmk[16] = {};
static uint32_t         _beacon_last_ms   = 0;
static uint32_t         _code_start_ms    = 0;
static bool             _probe_active     = false;
static uint8_t          _probe_idx        = 0;

static bool     _reconnect_active    = false;
static uint8_t  _reconnect_probe_idx = 0;
static uint32_t _reconnect_beacon_ms = 0;
static uint32_t _reconnect_start_ms  = 0;

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return a && b && memcmp(a, b, 6) == 0;
}


static uint8_t             _rx_buf[RX_BUF_SIZE];
static std::atomic<int>    _rx_head {0};
static int                 _rx_tail = 0;

// Fragment reassembly (recv callback only)
static uint8_t  _frag_buf[MAX_FRAGS][FRAG_PAYLOAD_MAX];
static uint8_t  _frag_len[MAX_FRAGS];
static uint8_t  _frag_got;
static uint8_t  _frag_total;
static uint8_t  _frag_seq;
static bool     _frag_pending;
static uint32_t _frag_start_ms;

static uint8_t _tx_buf[TX_BUF_SIZE];
static int     _tx_len = 0;
static uint8_t _tx_seq = 0;

static void derive_lmk(const char* code, uint8_t out_lmk[16]) {
    const char* prefix     = "fluiddial-espnow:";
    size_t      prefix_len = strlen(prefix);
    size_t      code_len   = strlen(code);
    uint8_t     input[48]; // prefix (17) + code (6) + margin
    memcpy(input, prefix, prefix_len);
    memcpy(input + prefix_len, code, code_len);
    uint8_t hash[32];
    mbedtls_sha256(input, prefix_len + code_len, hash, 0);
    memcpy(out_lmk, hash, 16);
}

static void derive_device_code(char out[9]) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    const char*             salt = "fluiddial-code-v2:";
    uint8_t                 hash[32];
    mbedtls_sha256_context  ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)salt, strlen(salt));
    mbedtls_sha256_update(&ctx, mac, 6);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    uint32_t n = ((uint32_t)hash[0] << 24 | (uint32_t)hash[1] << 16 |
                  (uint32_t)hash[2] << 8  | (uint32_t)hash[3]) % 100000000u;
    snprintf(out, 9, "%08u", n);
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

static inline void rx_push(uint8_t c) {
    int cur  = _rx_head.load(std::memory_order_relaxed);
    int next = (cur + 1) % RX_BUF_SIZE;
    if (next != _rx_tail) {
        _rx_buf[cur] = c;
        _rx_head.store(next, std::memory_order_release);
    }
}

static inline int rx_pop() {
    int head = _rx_head.load(std::memory_order_acquire);
    if (head == _rx_tail) return -1;
    uint8_t c = _rx_buf[_rx_tail];
    _rx_tail  = (_rx_tail + 1) % RX_BUF_SIZE;
    return (unsigned char)c;
}

static void send_fragments(const uint8_t* data, size_t len) {
    if (!_is_paired || len == 0) return;

    uint8_t total = (uint8_t)((len + FRAG_PAYLOAD_MAX - 1) / FRAG_PAYLOAD_MAX);
    if (total == 0) total = 1;
    if (total > MAX_FRAGS) {
        dbg_printf("ESP-NOW: TX message %u B exceeds max (%u B) — truncating\n",
                   (unsigned)len, (unsigned)(MAX_FRAGS * FRAG_PAYLOAD_MAX));
        total = MAX_FRAGS;
        len   = MAX_FRAGS * FRAG_PAYLOAD_MAX;
    }

    uint8_t seq    = _tx_seq++;
    uint8_t pkt[250];
    size_t  offset = 0;
    for (uint8_t i = 0; i < total; i++) {
        size_t chunk = len - offset;
        if (chunk > FRAG_PAYLOAD_MAX) chunk = FRAG_PAYLOAD_MAX;
        pkt[0] = PKT_DATA;
        pkt[1] = seq;
        pkt[2] = i;
        pkt[3] = total;
        memcpy(pkt + FRAG_HEADER_SIZE, data + offset, chunk);
        esp_now_send(_peer_mac, pkt, FRAG_HEADER_SIZE + chunk);
        offset += chunk;
    }
}

static void complete_pairing_from_ack(const PairAckPkt& ack) {
    memcpy(_peer_mac, ack.mac, 6);
    memcpy(_lmk, _pairing_lmk, 16);
    _op_channel = (ack.channel > 0 && ack.channel <= 14)
                  ? ack.channel : ESPNOW_PAIR_CHANNEL;

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
}

#if ESP_IDF_VERSION_MAJOR >= 5
static void on_data_recv(const esp_now_recv_info_t* recv_info,
                         const uint8_t* data, int len) {
    const uint8_t* src = recv_info ? recv_info->src_addr : nullptr;
    if (_is_paired && mac_eq(src, _peer_mac) && recv_info && recv_info->rx_ctrl) {
        update_rssi((int8_t)recv_info->rx_ctrl->rssi);
    }
#else
static void on_data_recv(const uint8_t* src,
                         const uint8_t* data, int len) {
#endif
    if (len < 1) return;
    uint8_t pkt_type = data[0];

    // pair handshake
    if (pkt_type == PKT_PAIR_ACK && _pairing_active
        && len >= (int)sizeof(PairAckPkt)) {
        memcpy(&_pair_ack_pending_pkt, data, sizeof(PairAckPkt));
        _pair_ack_pending.store(true, std::memory_order_release);
        return;
    }

    if (!_is_paired) return;
    if (!mac_eq(src, _peer_mac)) return;

    // Refresh liveness timestamp on packet rx
    _last_rx_ms   = millis();
    _is_connected = true;

    if (pkt_type == PKT_KEEPALIVE) {
        if (len >= 2) {
            update_rssi((int8_t)data[1]);
        }
        update_rx_time();
        return;
    }

    if (pkt_type == PKT_REALTIME && len >= 2) {
        rx_push(data[1]);
        update_rx_time();
        return;
    }

    // DATA FRAG
    if (pkt_type == PKT_DATA && len >= FRAG_HEADER_SIZE) {
        const FragHeader* hdr   = (const FragHeader*)data;
        uint8_t           idx   = hdr->frag_idx;
        uint8_t           total = hdr->total_frags;
        uint8_t           seq   = hdr->seq;
        int               plen  = len - FRAG_HEADER_SIZE;

        if (total == 0 || total > MAX_FRAGS || idx >= total
            || plen < 0 || plen > FRAG_PAYLOAD_MAX) return;

        if (_frag_pending) {
            bool stale_seq     = (_frag_seq != seq);
            bool stale_timeout = ((uint32_t)(millis() - _frag_start_ms)
                                  > FRAG_REASSEMBLY_TIMEOUT_MS);
            if (stale_seq || stale_timeout) {
                _frag_pending = false;
            }
        }

        if (!_frag_pending) {
            _frag_got      = 0;
            _frag_total    = total;
            _frag_seq      = seq;
            _frag_pending  = true;
            _frag_start_ms = millis();
            memset(_frag_len, 0, sizeof(_frag_len));
        }

        if (_frag_total != total) return;

        memcpy(_frag_buf[idx], data + FRAG_HEADER_SIZE, plen);
        _frag_len[idx]  = (uint8_t)plen;
        _frag_got      |= (uint8_t)(1u << idx);

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

#if ESP_IDF_VERSION_MAJOR < 5
static void promiscuous_rx_cb(void* buf, wifi_promiscuous_pkt_type_t /*type*/) {
    if (!_is_paired) return;
    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    if (pkt->rx_ctrl.sig_len < 16) return;
    if (memcmp(pkt->payload + 10, _peer_mac, 6) == 0) {
        update_rssi(pkt->rx_ctrl.rssi);
    }
}
#endif

static void on_data_sent(const uint8_t* /*mac*/, esp_now_send_status_t /*status*/) {}

void espnow_init() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

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

#if ESP_IDF_VERSION_MAJOR < 5
    {
        wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);
        esp_wifi_set_promiscuous(true);
    }
#endif

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

    if (_pair_ack_pending.load(std::memory_order_acquire)) {
        PairAckPkt ack;
        memcpy(&ack, &_pair_ack_pending_pkt, sizeof(ack));
        _pair_ack_pending.store(false, std::memory_order_release);
        complete_pairing_from_ack(ack);
        if (current_scene) current_scene->reDisplay();
    }

    if (_is_connected && (now - _last_rx_ms) > CONNECT_TIMEOUT_MS) {
        _is_connected = false;
        set_disconnected_state();
        if (current_scene) current_scene->reDisplay();
    }

    static bool _was_connected = false;
    if (_is_connected != _was_connected) {
        _was_connected = _is_connected;
        if (_is_connected) {
            fnc_realtime(StatusReport);
        }
        if (current_scene) current_scene->reDisplay();
    }

    if (state == Disconnected) {
        static uint32_t _badge_tick_ms = 0;
        if ((now - _badge_tick_ms) >= 400) {
            _badge_tick_ms = now;
            if (current_scene) current_scene->reDisplay();
        }
    }

    // Reconnect channel scan
    if (_is_paired && !_is_connected) {
        if (!_reconnect_active) {
            _reconnect_active    = true;
            _reconnect_probe_idx = 0;
            _reconnect_beacon_ms = 0;
            _reconnect_start_ms  = now;
            dbg_printf("ESP-NOW: reconnect started — saved ch=%d\n", _op_channel);
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
            dbg_printf("ESP-NOW: probe #%d ch=%d (+%lums)\n",
                       _reconnect_probe_idx, ch, (unsigned long)(now - _reconnect_start_ms));
        }
    } else if (_reconnect_active && _is_connected) {
        _reconnect_active = false;
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        if (current_scene) current_scene->reDisplay();
        Preferences prefs;
        prefs.begin(PREF_NAMESPACE, false);
        prefs.putUChar("espnow_ch", _op_channel);
        prefs.end();
        dbg_printf("ESP-NOW: reconnected on ch=%d (RSSI %d dBm)\n",
                   _op_channel, (int)_peer_rssi);
    }

    // Keepalive heartbeat
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
        size_t flush_len = (size_t)_tx_len;
        _tx_len = 0;

        send_fragments(_tx_buf, flush_len);
    }
}

int espnow_getchar() {
    return rx_pop();
}

bool espnow_rx_available() {
    return _rx_head.load(std::memory_order_acquire) != _rx_tail;
}

bool        espnow_is_paired()      { return _is_paired; }
bool        espnow_is_connected()   { return _is_paired && _is_connected; }
bool        espnow_is_reconnecting(){ return _reconnect_active; }
int8_t      espnow_rssi()           { return _peer_rssi; }

int espnow_signal_bars() {
    if (!_is_connected) return 0;
    int8_t r = _peer_rssi;
    if (r >= -55) return 4;
    if (r >= -65) return 3;
    if (r >= -75) return 2;
    if (r >= -85) return 1;
    return 0;
}

const char* espnow_status_str() {
    if (!_is_paired)       return "Not Paired";
    if (_reconnect_active) return "Searching...";
    if (_is_connected)     return "Connected";
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

    dbg_printf("ESP-NOW: pairing session started, code=%s\n", _pairing_code);
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
    _is_paired        = false;
    _is_connected     = false;
    _reconnect_active = false;
    _peer_rssi        = -100;

    _tx_len = 0;

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.remove("espnow_mac");
    prefs.remove("espnow_lmk");
    prefs.remove("espnow_ch");
    prefs.end();
    dbg_println("ESP-NOW: pairing cleared");
}

#else  // !USE_ESPNOW

// ESP-NOW is excluded from this build. Provide inert stubs so the rest of the
// firmware (transport dispatch, status UI, pairing scene) links and runs with
// ESP-NOW simply unavailable. wifi_get_transport() never reports ESPNOW in this
// configuration, so none of these are reached at runtime
void        espnow_init() {}
void        espnow_poll() {}
void        espnow_putchar(uint8_t) {}
int         espnow_getchar() { return -1; }
bool        espnow_rx_available() { return false; }
bool        espnow_is_paired() { return false; }
bool        espnow_is_connected() { return false; }
const char* espnow_status_str() { return ""; }
const char* espnow_start_pairing() { return ""; }
void        espnow_cancel_pairing() {}
bool        espnow_pairing_complete() { return false; }
const char* espnow_pairing_code() { return ""; }
uint32_t    espnow_code_remaining_ms() { return 0; }
void        espnow_clear_pairing() {}
bool        espnow_has_saved_pairing() { return false; }
bool        espnow_is_reconnecting() { return false; }
int8_t      espnow_rssi() { return -100; }
int         espnow_signal_bars() { return 0; }

#endif  // USE_ESPNOW

#endif  // USE_WIFI
#endif  // ARDUINO
