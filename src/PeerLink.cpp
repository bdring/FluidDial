// 2026 - Figamore

#ifdef ARDUINO
#ifdef USE_WIFI

#include "PeerLink.h"

#ifdef USE_ESPNOW

#include "ESPNowCrypto.h"
#include "FluidNCModel.h"
#include "System.h"
#include "Scene.h"

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <atomic>

#define PREF_NAMESPACE             "fluidespnow"
#define ESPNOW_PAIR_CHANNEL        1
#define BEACON_INTERVAL_MS         2000
#define PAIRING_SESSION_LIFETIME_MS 60000
#define CONNECT_TIMEOUT_MS         5000
#define KEEPALIVE_INTERVAL_MS      2000
#define RECONNECT_PROBE_MS         1000
#define RX_BUF_SIZE                2048
#define TX_BUF_SIZE                512
#define MAX_FRAGS                  8
#define MAX_MACHINE_PROFILES       5
#define FRAG_HEADER_SIZE           12  // type + nonce(4) + counter(4) + seq + idx + total
#define FRAG_PAYLOAD_MAX           (250 - FRAG_HEADER_SIZE)  // 238 bytes
#define ART_TAG_SIZE               8   // anti-replay tag: nonce(4) + counter(4)
#define PAIR_TAG_SIZE              16  // HMAC-SHA256 truncated tag for pairing packets
#define FRAG_REASSEMBLY_TIMEOUT_MS 3000   // discard stale partial reassembly

#define PKT_DISCOVERY  0x01
#define PKT_PAIR_ACK   0x02
#define PKT_DATA       0x03
#define PKT_PAIR_CONFIRM 0x04
#define PKT_REALTIME   0x05
#define PKT_KEEPALIVE  0x06

static const uint8_t PROBE_ORDER[13] = {6, 11, 1, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static constexpr uint8_t PAIRING_PROTO_V3 = 3;
static constexpr uint8_t PAIRING_MODE_PAIR = 1;
static constexpr size_t  ESPNOW_HOSTNAME_SIZE = ESPNOW_PROFILE_HOSTNAME_SIZE;

struct __attribute__((packed)) DiscoveryV3Pkt {
    uint8_t type;       // PKT_DISCOVERY
    uint8_t version;    // PAIRING_PROTO_V3
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidDial MAC
    uint8_t channel;    // channel FluidDial is currently using
    uint8_t pair_nonce[4];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
};

struct __attribute__((packed)) PairChallengeV3Pkt {
    uint8_t type;       // PKT_PAIR_ACK
    uint8_t version;    // PAIRING_PROTO_V3
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidNC MAC
    uint8_t channel;    // FluidNC's operational WiFi channel
    uint8_t pair_nonce[4];
    uint8_t pubkey[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE];
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) PairConfirmV3Pkt {
    uint8_t type;       // PKT_PAIR_CONFIRM
    uint8_t version;    // PAIRING_PROTO_V3
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidDial MAC
    uint8_t pair_nonce[4];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) PairResultV3Pkt {
    uint8_t type;       // PKT_PAIR_ACK
    uint8_t version;    // PAIRING_PROTO_V3
    uint8_t mode;       // PAIRING_MODE_PAIR
    uint8_t mac[6];     // FluidNC MAC
    uint8_t channel;    // FluidNC's operational WiFi channel
    uint8_t pair_nonce[4];
    char hostname[ESPNOW_HOSTNAME_SIZE];
    uint8_t auth_tag[PAIR_TAG_SIZE];
};

struct __attribute__((packed)) FragHeader {
    uint8_t type;         // PKT_DATA
    uint8_t nonce[4];     // receiver's current challenge (anti-replay)
    uint8_t counter[4];   // sender's monotonic counter, little-endian (anti-replay)
    uint8_t seq;
    uint8_t frag_idx;
    uint8_t total_frags;
};

struct __attribute__((packed)) StoredMachineProfile {
    uint8_t version;
    uint8_t mac[6];
    uint8_t lmk[16];
    uint8_t channel;
    char hostname[ESPNOW_HOSTNAME_SIZE];
};

struct __attribute__((packed)) StoredMachineProfileList {
    uint8_t version;
    uint8_t active;
    uint8_t count;
    uint8_t reserved;
    StoredMachineProfile profiles[MAX_MACHINE_PROFILES];
};

static constexpr uint8_t PROFILE_STORE_VERSION = 1;

static uint8_t          _peer_mac[6]  = {};
static uint8_t          _lmk[16]      = {};
static uint8_t          _op_channel   = ESPNOW_PAIR_CHANNEL;
static uint8_t          _preferred_channel = ESPNOW_PAIR_CHANNEL;
static volatile bool    _is_paired    = false;
static std::atomic<uint32_t> _pairing_nonce {0};

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
static PairResultV3Pkt  _pair_ack_pending_pkt = {};
static char             _peer_hostname[ESPNOW_HOSTNAME_SIZE] = {};
static uint8_t          _pairing_lmk[16] = {};
static uint8_t          _pairing_window_lmk[16] = {};
static uint8_t          _pairing_private_key[ESPNowCrypto::ECDH_PRIVATE_KEY_SIZE] = {};
static uint8_t          _pairing_public_key[ESPNowCrypto::ECDH_PUBLIC_KEY_SIZE] = {};
static bool             _pairing_keypair_valid = false;
static uint32_t         _beacon_last_ms   = 0;
static uint32_t         _pairing_start_ms = 0;
static bool             _probe_active     = false;
static uint8_t          _probe_idx        = 0;

static bool     _reconnect_active    = false;
static uint8_t  _reconnect_probe_idx = 0;
static uint32_t _reconnect_beacon_ms = 0;
static uint32_t _reconnect_start_ms  = 0;
static uint8_t  _reconnect_saved_ch  = ESPNOW_PAIR_CHANNEL;
static std::atomic<uint8_t> _rx_channel_hint {0};

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool mac_eq(const uint8_t* a, const uint8_t* b) {
    return a && b && memcmp(a, b, 6) == 0;
}

static bool valid_profile_list(const StoredMachineProfileList& list) {
    return list.version == PROFILE_STORE_VERSION &&
           list.count <= MAX_MACHINE_PROFILES &&
           (list.count == 0 || list.active < list.count);
}

static bool load_profiles(Preferences& prefs, StoredMachineProfileList& list) {
    memset(&list, 0, sizeof(list));
    if (!prefs.isKey("profiles")) {
        return false;
    }
    if (prefs.getBytesLength("profiles") != sizeof(list)) {
        return false;
    }
    if (prefs.getBytes("profiles", &list, sizeof(list)) != sizeof(list)) {
        memset(&list, 0, sizeof(list));
        return false;
    }
    if (!valid_profile_list(list)) {
        memset(&list, 0, sizeof(list));
        return false;
    }
    return true;
}

static void init_profiles(StoredMachineProfileList& list) {
    memset(&list, 0, sizeof(list));
    list.version = PROFILE_STORE_VERSION;
}

static bool load_active_profile(Preferences& prefs) {
    StoredMachineProfileList list;
    if (!load_profiles(prefs, list) || list.count == 0) {
        return false;
    }

    const StoredMachineProfile& profile = list.profiles[list.active];
    if (profile.version != PROFILE_STORE_VERSION) {
        return false;
    }

    memcpy(_peer_mac, profile.mac, sizeof(_peer_mac));
    memcpy(_lmk, profile.lmk, sizeof(_lmk));
    _preferred_channel = profile.channel ? profile.channel : ESPNOW_PAIR_CHANNEL;
    strlcpy(_peer_hostname, profile.hostname, sizeof(_peer_hostname));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    return true;
}

static void store_active_profile(Preferences& prefs,
                                 const uint8_t mac[6],
                                 const uint8_t lmk[16],
                                 uint8_t channel,
                                 const char hostname[ESPNOW_HOSTNAME_SIZE]) {
    StoredMachineProfileList list;
    if (!load_profiles(prefs, list)) {
        init_profiles(list);
    }

    int slot = -1;
    for (uint8_t i = 0; i < list.count; ++i) {
        if (memcmp(list.profiles[i].mac, mac, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (list.count < MAX_MACHINE_PROFILES) {
            slot = list.count++;
        } else {
            slot = list.active < MAX_MACHINE_PROFILES ? list.active : 0;
        }
    }

    StoredMachineProfile& profile = list.profiles[slot];
    memset(&profile, 0, sizeof(profile));
    profile.version = PROFILE_STORE_VERSION;
    memcpy(profile.mac, mac, 6);
    memcpy(profile.lmk, lmk, 16);
    profile.channel = channel;
    strlcpy(profile.hostname, hostname ? hostname : "", sizeof(profile.hostname));
    list.active = (uint8_t)slot;

    prefs.putBytes("profiles", &list, sizeof(list));
    ESPNowCrypto::secureZero(&list, sizeof(list));
}

static void profile_to_info(const StoredMachineProfileList& list, uint8_t index, ESPNowProfileInfo& out) {
    memset(&out, 0, sizeof(out));
    if (index >= list.count) {
        return;
    }
    const StoredMachineProfile& profile = list.profiles[index];
    memcpy(out.mac, profile.mac, sizeof(out.mac));
    out.channel = profile.channel;
    out.active = index == list.active;
    strlcpy(out.hostname, profile.hostname, sizeof(out.hostname));
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

// ---- anti-replay ----------------------------------------------------------
// Each side issues a random 32-bit "challenge" nonce for the traffic it
// receives and advertises it in its keepalives. The peer stamps every
// DATA/REALTIME frame with challenge; (a monotonic 32 bit counter). The
// receiver accepts a frame only if the nonce matches its current challenge and
// the counter is ahead of a 64-entry sliding window, so a captured frame can't
// be replayed. A fresh challenge is issued on every reconnect.
static std::atomic<uint32_t> _rx_nonce      {0};  // challenge to the peer
static std::atomic<uint32_t> _tx_peer_nonce {0};  // peer's challenge, learned via keepalive
static std::atomic<bool>     _tx_peer_known {false};
static std::atomic<uint32_t> _tx_counter    {0};  // monotonic send counter

// receiver sliding-window state - touched only in the recv callback
static ESPNowCrypto::ReplayState _rx_replay;

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

static void set_connected_now() {
    _last_rx_ms   = millis();
    _is_connected = true;
}

static uint8_t current_wifi_channel() {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK) {
        return 0;
    }
    return primary;
}

static void note_rx_channel(uint8_t channel) {
    if (channel >= 1 && channel <= 14) {
        _rx_channel_hint.store(channel, std::memory_order_release);
    }
}

static void send_keepalive(const uint8_t mac[6]) {
    uint32_t n = _rx_nonce.load(std::memory_order_acquire);
    if (!_tx_peer_known.load(std::memory_order_acquire)) {
        uint8_t pkt[5];
        pkt[0] = PKT_KEEPALIVE;
        memcpy(pkt + 1, &n, 4);
        esp_now_send(mac, pkt, sizeof(pkt));
        return;
    }

    uint8_t pkt[1 + 4 + ART_TAG_SIZE];
    pkt[0] = PKT_KEEPALIVE;
    memcpy(pkt + 1, &n, 4);   // auth'd advertisement of the challenge
    if (!ESPNowCrypto::stampAntiReplayTag(_tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 5)) return;
    esp_now_send(mac, pkt, sizeof(pkt));
}

static int accept_keepalive(const uint8_t* data, int len) {
    if (len >= 1 + 4 + ART_TAG_SIZE) {
        uint32_t advertised, nonce, counter;
        memcpy(&advertised, data + 1, 4);
        memcpy(&nonce,      data + 5, 4);
        memcpy(&counter,    data + 9, 4);
        if (!ESPNowCrypto::acceptReplay(_rx_nonce, _rx_replay, nonce, counter, millis()) || advertised == 0) return 0;
        _tx_peer_nonce.store(advertised, std::memory_order_release);
        _tx_peer_known.store(true, std::memory_order_release);
        return 2;
    }

    if (len >= 5 && !_tx_peer_known.load(std::memory_order_acquire)) {
        uint32_t advertised;
        memcpy(&advertised, data + 1, 4);
        if (advertised == 0) return 0;
        _tx_peer_nonce.store(advertised, std::memory_order_release);
        _tx_peer_known.store(true, std::memory_order_release);
        return 1;
    }

    return 0;
}

static bool valid_pair_nonce(const uint8_t pair_nonce[4]) {
    uint32_t expected_nonce = _pairing_nonce.load(std::memory_order_acquire);
    uint32_t rx_nonce;
    memcpy(&rx_nonce, pair_nonce, 4);
    return rx_nonce != 0 && rx_nonce == expected_nonce;
}

static void build_discovery_v3(DiscoveryV3Pkt& pkt, const uint8_t my_mac[6]) {
    uint32_t pairing_nonce = _pairing_nonce.load(std::memory_order_acquire);
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = PKT_DISCOVERY;
    pkt.version = PAIRING_PROTO_V3;
    pkt.mode = PAIRING_MODE_PAIR;
    memcpy(pkt.mac, my_mac, 6);
    pkt.channel = _op_channel;
    memcpy(pkt.pair_nonce, &pairing_nonce, 4);
    memcpy(pkt.pubkey, _pairing_public_key, sizeof(pkt.pubkey));
}

static bool accept_pair_result(const uint8_t* src, const uint8_t* data, int len) {
    if (!_pairing_active || len != (int)sizeof(PairResultV3Pkt)) return false;

    PairResultV3Pkt result;
    memcpy(&result, data, sizeof(result));
    if (result.version != PAIRING_PROTO_V3 || result.mode != PAIRING_MODE_PAIR || !mac_eq(src, result.mac)) return false;
    if (!valid_pair_nonce(result.pair_nonce)) return false;

    if (!ESPNowCrypto::verifyPairingAuthTag(_pairing_lmk,
                                            reinterpret_cast<const uint8_t*>(&result),
                                            offsetof(PairResultV3Pkt, auth_tag),
                                            result.auth_tag)) {
        return false;
    }

    memcpy(&_pair_ack_pending_pkt, &result, sizeof(result));
    _pair_ack_pending.store(true, std::memory_order_release);
    return true;
}

static bool accept_pair_challenge(const uint8_t* src, const uint8_t* data, int len) {
    if (!_pairing_active || !_pairing_keypair_valid || len != (int)sizeof(PairChallengeV3Pkt)) return false;

    PairChallengeV3Pkt challenge;
    memcpy(&challenge, data, sizeof(challenge));
    if (challenge.version != PAIRING_PROTO_V3 || challenge.mode != PAIRING_MODE_PAIR || !mac_eq(src, challenge.mac)) return false;
    if (!valid_pair_nonce(challenge.pair_nonce) || challenge.pubkey[0] != 0x04) return false;

    uint8_t shared_secret[ESPNowCrypto::ECDH_SHARED_SECRET_SIZE];
    if (!ESPNowCrypto::deriveEcdhSharedSecret(_pairing_private_key, challenge.pubkey, shared_secret)) {
        return false;
    }

    uint8_t my_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, my_mac);
    DiscoveryV3Pkt discovery;
    build_discovery_v3(discovery, my_mac);

    ESPNowCrypto::derivePairingSessionLmk(_pairing_window_lmk,
                                          shared_secret,
                                          reinterpret_cast<const uint8_t*>(&discovery),
                                          sizeof(discovery),
                                          reinterpret_cast<const uint8_t*>(&challenge),
                                          sizeof(challenge),
                                          _pairing_lmk);

    PairConfirmV3Pkt confirm = {};
    confirm.type = PKT_PAIR_CONFIRM;
    confirm.version = PAIRING_PROTO_V3;
    confirm.mode = PAIRING_MODE_PAIR;
    memcpy(confirm.mac, my_mac, sizeof(confirm.mac));
    memcpy(confirm.pair_nonce, challenge.pair_nonce, sizeof(confirm.pair_nonce));
    ESPNowCrypto::pairingAuthTag(_pairing_lmk,
                                 reinterpret_cast<const uint8_t*>(&confirm),
                                 offsetof(PairConfirmV3Pkt, auth_tag),
                                 confirm.auth_tag);

    register_peer(challenge.mac, challenge.channel, false, nullptr);
    esp_now_send(challenge.mac, reinterpret_cast<const uint8_t*>(&confirm), sizeof(confirm));

    ESPNowCrypto::secureZero(shared_secret, sizeof(shared_secret));
    ESPNowCrypto::secureZero(&discovery, sizeof(discovery));
    ESPNowCrypto::secureZero(&challenge, sizeof(challenge));
    ESPNowCrypto::secureZero(&confirm, sizeof(confirm));
    return true;
}

static void send_realtime(uint8_t c) {
    uint8_t pkt[1 + ART_TAG_SIZE + 1];
    pkt[0] = PKT_REALTIME;
    if (!ESPNowCrypto::stampAntiReplayTag(_tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 1)) return;   // peer challenge not learned yet -> drop
    pkt[1 + ART_TAG_SIZE] = c;
    esp_now_send(_peer_mac, pkt, sizeof(pkt));
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
        if (!ESPNowCrypto::stampAntiReplayTag(_tx_peer_known, _tx_peer_nonce, _tx_counter, pkt + 1)) return;   // peer challenge not learned yet -> drop
        pkt[9]  = seq;                     // 1 + ART_TAG_SIZE
        pkt[10] = i;
        pkt[11] = total;
        memcpy(pkt + FRAG_HEADER_SIZE, data + offset, chunk);
        esp_now_send(_peer_mac, pkt, FRAG_HEADER_SIZE + chunk);
        offset += chunk;
    }
}

static void complete_pairing_from_ack(const PairResultV3Pkt& ack) {
    memcpy(_peer_mac, ack.mac, 6);
    memcpy(_lmk, _pairing_lmk, 16);
    memcpy(_peer_hostname, ack.hostname, sizeof(_peer_hostname));
    _peer_hostname[sizeof(_peer_hostname) - 1] = '\0';
    _preferred_channel = (ack.channel > 0 && ack.channel <= 14)
                         ? ack.channel : ESPNOW_PAIR_CHANNEL;
    _op_channel = _preferred_channel;

    register_peer(_peer_mac, _op_channel, true, _lmk);
    esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    store_active_profile(prefs, _peer_mac, _lmk, _preferred_channel, _peer_hostname);
    prefs.end();

    _is_paired        = true;
    _pairing_complete = true;
    _pairing_active   = false;
    _probe_active     = false;
    _pairing_keypair_valid = false;
    ESPNowCrypto::secureZero(_pairing_private_key, sizeof(_pairing_private_key));
    ESPNowCrypto::secureZero(_pairing_public_key, sizeof(_pairing_public_key));
    ESPNowCrypto::secureZero(_pairing_window_lmk, sizeof(_pairing_window_lmk));
    ESPNowCrypto::secureZero(_pairing_lmk, sizeof(_pairing_lmk));
    set_connected_now();

    // Start a fresh anti-replay session + re-learn the peer's challenge from
    // its first keepalive before sending any DATA/REALTIME frames.
    _tx_peer_known.store(false, std::memory_order_release);
    ESPNowCrypto::issueRxChallenge(_rx_nonce);

    dbg_printf("ESP-NOW: pairing complete — peer %02x:%02x:%02x:%02x:%02x:%02x host=%s ch=%d\n",
               _peer_mac[0], _peer_mac[1], _peer_mac[2],
               _peer_mac[3], _peer_mac[4], _peer_mac[5], _peer_hostname, _op_channel);
}

#if ESP_IDF_VERSION_MAJOR >= 5
static void on_data_recv(const esp_now_recv_info_t* recv_info,
                         const uint8_t* data, int len) {
    const uint8_t* src = recv_info ? recv_info->src_addr : nullptr;
    uint8_t rx_channel = (recv_info && recv_info->rx_ctrl) ? recv_info->rx_ctrl->channel : 0;
    if (_is_paired && mac_eq(src, _peer_mac) && recv_info && recv_info->rx_ctrl) {
        update_rssi((int8_t)recv_info->rx_ctrl->rssi);
    }
#else
static void on_data_recv(const uint8_t* src,
                         const uint8_t* data, int len) {
    uint8_t rx_channel = current_wifi_channel();
#endif
    if (len < 1) return;
    uint8_t pkt_type = data[0];

    // pair handshake
    if (pkt_type == PKT_PAIR_ACK && len == (int)sizeof(PairChallengeV3Pkt) &&
        accept_pair_challenge(src, data, len)) {
        return;
    }
    if (pkt_type == PKT_PAIR_ACK && len == (int)sizeof(PairResultV3Pkt) &&
        accept_pair_result(src, data, len)) {
        return;
    }

    // Pairing owns the radio channel. Ignore traffic from the previously
    // selected profile until pairing completes or is cancelled.
    if (_pairing_active) return;

    if (!_is_paired) return;
    if (!mac_eq(src, _peer_mac)) return;

    if (pkt_type == PKT_KEEPALIVE) {
        int keepalive_state = accept_keepalive(data, len);
        if (keepalive_state == 2) {
            note_rx_channel(rx_channel);
            set_connected_now();
            update_rx_time();
        }
        return;
    }

    if (pkt_type == PKT_REALTIME && len >= 1 + ART_TAG_SIZE + 1) {
        uint32_t nonce, counter;
        memcpy(&nonce,   data + 1, 4);
        memcpy(&counter, data + 5, 4);
        if (!ESPNowCrypto::acceptReplay(_rx_nonce, _rx_replay, nonce, counter, millis())) return;   // replay / stale -> drop
        note_rx_channel(rx_channel);
        set_connected_now();
        rx_push(data[1 + ART_TAG_SIZE]);
        update_rx_time();
        return;
    }

    // DATA FRAG
    if (pkt_type == PKT_DATA && len >= FRAG_HEADER_SIZE) {
        const FragHeader* hdr   = (const FragHeader*)data;
        uint32_t          nonce, counter;
        memcpy(&nonce,   hdr->nonce,   4);
        memcpy(&counter, hdr->counter, 4);
        if (!ESPNowCrypto::acceptReplay(_rx_nonce, _rx_replay, nonce, counter, millis())) return;   // replay / stale ->>> drop
        note_rx_channel(rx_channel);
        set_connected_now();

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
        uint8_t pmk[16];
        ESPNowCrypto::derivePmk(pmk);
        esp_now_set_pmk(pmk);
    }

    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);

    ESPNowCrypto::issueRxChallenge(_rx_nonce);   // anti-replay challenge advertised in keepalives

#if ESP_IDF_VERSION_MAJOR < 5
    {
        wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
        esp_wifi_set_promiscuous_filter(&filt);
        esp_wifi_set_promiscuous_rx_cb(promiscuous_rx_cb);
        esp_wifi_set_promiscuous(true);
    }
#endif

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    if (load_active_profile(prefs)) {
        _op_channel = _preferred_channel;
        prefs.end();

        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
        register_peer(_peer_mac, _op_channel, true, _lmk);
        _is_paired = true;
        dbg_printf("ESP-NOW: loaded pairing, ch=%d\n", _op_channel);
    } else {
        prefs.end();
        dbg_println("ESP-NOW: no saved pairing");
    }

    _pairing_nonce.store(ESPNowCrypto::randomNonce(), std::memory_order_release);
}

void espnow_poll() {
    uint32_t now = millis();

    uint8_t rx_channel = _rx_channel_hint.exchange(0, std::memory_order_acq_rel);
    if (!_pairing_active && rx_channel >= 1 && rx_channel <= 14 && rx_channel != _op_channel) {
        _preferred_channel = rx_channel;
        _op_channel = rx_channel;
        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
        if (_is_paired) {
            register_peer(_peer_mac, _op_channel, true, _lmk);
        }
    }

    if (_pair_ack_pending.load(std::memory_order_acquire)) {
        PairResultV3Pkt ack;
        memcpy(&ack, &_pair_ack_pending_pkt, sizeof(ack));
        _pair_ack_pending.store(false, std::memory_order_release);
        complete_pairing_from_ack(ack);
        if (current_scene) current_scene->reDisplay();
    }

    if (!_pairing_active && _is_connected && (now - _last_rx_ms) > CONNECT_TIMEOUT_MS) {
        _is_connected = false;
        // Roll the anti-replay session: a new challenge invalidates frames the
        // peer stamped for the previous session + force to re-learn the
        // peer's challenge before sending again
        _tx_peer_known.store(false, std::memory_order_release);
        ESPNowCrypto::issueRxChallenge(_rx_nonce);
        set_disconnected_state();
        if (current_scene) current_scene->reDisplay();
    }

    static bool _was_connected = false;
    if (_is_connected != _was_connected) {
        _was_connected = _is_connected;
        if (current_scene) current_scene->reDisplay();
    }

    static bool _was_tx_known = false;
    bool tx_known = _tx_peer_known.load(std::memory_order_acquire);
    if (tx_known && !_was_tx_known) {
        fnc_realtime(StatusReport);
    }
    _was_tx_known = tx_known;

    if (state == Disconnected) {
        static uint32_t _badge_tick_ms = 0;
        if ((now - _badge_tick_ms) >= 400) {
            _badge_tick_ms = now;
            if (current_scene) current_scene->reDisplay();
        }
    }

    // Reconnect channel scan
    if (!_pairing_active && _is_paired && !_is_connected) {
        if (!_reconnect_active) {
            _reconnect_active    = true;
            _reconnect_probe_idx = 0;
            _reconnect_beacon_ms = 0;
            _reconnect_start_ms  = now;
            _reconnect_saved_ch  = _preferred_channel;
            dbg_printf("ESP-NOW: reconnect started — saved ch=%d\n", _preferred_channel);
            if (current_scene) current_scene->reDisplay();
        }

        if ((now - _reconnect_beacon_ms) >= RECONNECT_PROBE_MS) {
            _reconnect_beacon_ms = now;

            uint8_t ch;
            if (_reconnect_probe_idx < 3 && _reconnect_saved_ch >= 1 && _reconnect_saved_ch <= 13) {
                ch = _reconnect_saved_ch;
            } else {
                uint8_t sweep_idx = (uint8_t)((_reconnect_probe_idx - 3) % 13);
                ch = PROBE_ORDER[sweep_idx];
            }
            _reconnect_probe_idx++;

            _op_channel = ch;
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            register_peer(_peer_mac, ch, true, _lmk);

            send_keepalive(_peer_mac);
            send_keepalive(_peer_mac);
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
        _preferred_channel = _op_channel;
        store_active_profile(prefs, _peer_mac, _lmk, _preferred_channel, _peer_hostname);
        prefs.end();
        dbg_printf("ESP-NOW: reconnected on ch=%d (RSSI %d dBm)\n",
                   _op_channel, (int)_peer_rssi);
    }

    // Keepalive heartbeat
    if (!_pairing_active && _is_paired && _is_connected && (now - _keepalive_last_ms) >= KEEPALIVE_INTERVAL_MS) {
        _keepalive_last_ms = now;
        send_keepalive(_peer_mac);
    }

    if (!_pairing_active) return;

    if ((now - _pairing_start_ms) >= PAIRING_SESSION_LIFETIME_MS) {
        _pairing_start_ms = now;
        _pairing_nonce.store(ESPNowCrypto::randomNonce(), std::memory_order_release);
        _pairing_keypair_valid =
            ESPNowCrypto::generateEcdhKeypair(_pairing_private_key, _pairing_public_key);
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

        if (!_pairing_keypair_valid) {
            _pairing_keypair_valid =
                ESPNowCrypto::generateEcdhKeypair(_pairing_private_key, _pairing_public_key);
        }
        if (_pairing_keypair_valid) {
            DiscoveryV3Pkt pkt;
            build_discovery_v3(pkt, my_mac);
            esp_now_send(BROADCAST_MAC, (const uint8_t*)&pkt, sizeof(pkt));
            ESPNowCrypto::secureZero(&pkt, sizeof(pkt));
        }
    }
}

void espnow_putchar(uint8_t c) {
    if (c == 0x11 || c == 0x13) return;

    if (c == 0x18 || (c >= 0x80 && c <= 0x9F) || (c >= 0xB0 && c <= 0xB3)) {
        if (_is_paired) send_realtime(c);
        return;
    }
    if (_tx_len == 0 && (c == '?' || c == '!' || c == '~')) {
        if (_is_paired) send_realtime(c);
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

void espnow_start_pairing() {
    if (_is_paired && esp_now_is_peer_exist(_peer_mac)) {
        esp_now_del_peer(_peer_mac);
    }

    _is_connected = false;
    _reconnect_active = false;
    _tx_peer_known.store(false, std::memory_order_release);
    _pair_ack_pending.store(false, std::memory_order_release);

    _probe_active = true;
    _probe_idx    = 0;
    _op_channel   = PROBE_ORDER[0];
    esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);

    if (esp_now_is_peer_exist(BROADCAST_MAC)) {
        esp_now_del_peer(BROADCAST_MAC);
    }

    ESPNowCrypto::derivePairingWindowLmk(_pairing_window_lmk);
    _pairing_keypair_valid =
        ESPNowCrypto::generateEcdhKeypair(_pairing_private_key, _pairing_public_key);
    _pairing_nonce.store(ESPNowCrypto::randomNonce(), std::memory_order_release);

    _pairing_start_ms = millis();
    _pairing_active   = true;
    _pairing_complete = false;
    _beacon_last_ms   = 0;

    dbg_println("ESP-NOW: pairing window started");
}

void espnow_cancel_pairing() {
    _pairing_active = false;
    _probe_active   = false;
    _pairing_keypair_valid = false;
    ESPNowCrypto::secureZero(_pairing_private_key, sizeof(_pairing_private_key));
    ESPNowCrypto::secureZero(_pairing_public_key, sizeof(_pairing_public_key));
    ESPNowCrypto::secureZero(_pairing_window_lmk, sizeof(_pairing_window_lmk));
    ESPNowCrypto::secureZero(_pairing_lmk, sizeof(_pairing_lmk));

    if (_is_paired) {
        _op_channel = _preferred_channel;
        esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
        register_peer(_peer_mac, _op_channel, true, _lmk);
    }
    dbg_println("ESP-NOW: pairing cancelled");
}

bool        espnow_pairing_complete() { return _pairing_complete; }

bool espnow_has_saved_pairing() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    StoredMachineProfileList list;
    bool has = load_profiles(prefs, list) && list.count > 0;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return has;
}

size_t espnow_profile_count() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    StoredMachineProfileList list;
    size_t count = load_profiles(prefs, list) ? list.count : 0;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return count;
}

int espnow_active_profile_index() {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    StoredMachineProfileList list;
    int active = load_profiles(prefs, list) && list.count > 0 ? (int)list.active : -1;
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return active;
}

bool espnow_get_profile(size_t index, ESPNowProfileInfo& out) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    StoredMachineProfileList list;
    bool ok = load_profiles(prefs, list) && index < list.count;
    if (ok) {
        profile_to_info(list, (uint8_t)index, out);
    } else {
        memset(&out, 0, sizeof(out));
    }
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();
    return ok;
}

bool espnow_select_profile(size_t index) {
    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    StoredMachineProfileList list;
    if (!load_profiles(prefs, list) || index >= list.count) {
        ESPNowCrypto::secureZero(&list, sizeof(list));
        prefs.end();
        return false;
    }

    list.active = (uint8_t)index;
    const StoredMachineProfile& profile = list.profiles[list.active];

    if (_is_paired && esp_now_is_peer_exist(_peer_mac)) {
        esp_now_del_peer(_peer_mac);
    }

    memcpy(_peer_mac, profile.mac, sizeof(_peer_mac));
    memcpy(_lmk, profile.lmk, sizeof(_lmk));
    _preferred_channel = profile.channel ? profile.channel : ESPNOW_PAIR_CHANNEL;
    _op_channel = _preferred_channel;
    strlcpy(_peer_hostname, profile.hostname, sizeof(_peer_hostname));

    prefs.putBytes("profiles", &list, sizeof(list));
    ESPNowCrypto::secureZero(&list, sizeof(list));
    prefs.end();

    esp_wifi_set_channel(_op_channel, WIFI_SECOND_CHAN_NONE);
    register_peer(_peer_mac, _op_channel, true, _lmk);

    _is_paired = true;
    _is_connected = false;
    _reconnect_active = false;
    _pairing_active = false;
    _probe_active = false;
    _pairing_complete = false;
    _tx_len = 0;
    _tx_peer_known.store(false, std::memory_order_release);
    _tx_peer_nonce.store(0, std::memory_order_release);
    ESPNowCrypto::issueRxChallenge(_rx_nonce);
    set_disconnected_state();

    dbg_printf("ESP-NOW: selected profile %u - peer %02x:%02x:%02x:%02x:%02x:%02x host=%s ch=%d\n",
               (unsigned)index + 1,
               _peer_mac[0], _peer_mac[1], _peer_mac[2],
               _peer_mac[3], _peer_mac[4], _peer_mac[5], _peer_hostname, _op_channel);
    return true;
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

    _tx_peer_known.store(false, std::memory_order_release);
    _tx_peer_nonce.store(0, std::memory_order_release);
    ESPNowCrypto::issueRxChallenge(_rx_nonce);

    Preferences prefs;
    prefs.begin(PREF_NAMESPACE, false);
    prefs.remove("profiles");
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
void        espnow_start_pairing() {}
void        espnow_cancel_pairing() {}
bool        espnow_pairing_complete() { return false; }
void        espnow_clear_pairing() {}
bool        espnow_has_saved_pairing() { return false; }
bool        espnow_is_reconnecting() { return false; }
int8_t      espnow_rssi() { return -100; }
int         espnow_signal_bars() { return 0; }
size_t      espnow_profile_count() { return 0; }
int         espnow_active_profile_index() { return -1; }
bool        espnow_get_profile(size_t, ESPNowProfileInfo& out) { memset(&out, 0, sizeof(out)); return false; }
bool        espnow_select_profile(size_t) { return false; }

#endif  // USE_ESPNOW

#endif  // USE_WIFI
#endif  // ARDUINO
