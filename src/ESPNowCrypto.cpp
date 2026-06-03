// 2026 - Figamore

#ifdef ARDUINO
#ifdef USE_WIFI
#ifdef USE_ESPNOW

#include "ESPNowCrypto.h"

#include <esp_efuse.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace {

constexpr uint32_t kPBKDF2Iterations = 5000;
constexpr uint32_t kReplayRegenMs = 3000;
constexpr uint32_t kReplayUnderflowLimit = 8;
constexpr uint8_t kPairCodeKdfLabel[] = "fluiddial-espnow-v3";
constexpr char kEspNowPmkLabel[] = "fluiddial-espnow-pmk-v1";
constexpr char kDeviceCodeContext[] = "fluiddial-code-v3:";
constexpr char kB32Alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

bool loadOrCreateDeviceSecret(uint8_t out[8]) {
    esp_efuse_read_block(EFUSE_BLK3, out, 0, 8 * 8);

    bool empty = true;
    for (int i = 0; i < 8; ++i) {
        if (out[i]) {
            empty = false;
            break;
        }
    }

    if (!empty) {
        return false;
    }

    esp_fill_random(out, 8);
    esp_efuse_write_block(EFUSE_BLK3, out, 0, 8 * 8);
    return true;
}

}  // namespace

namespace ESPNowCrypto {

void deriveLmk(const char* code, uint8_t out_lmk[16]) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_pkcs5_pbkdf2_hmac(&ctx,
                              reinterpret_cast<const uint8_t*>(code), strlen(code),
                              kPairCodeKdfLabel, sizeof(kPairCodeKdfLabel) - 1,
                              kPBKDF2Iterations, 16, out_lmk);
    mbedtls_md_free(&ctx);
}

void derivePmk(uint8_t out_pmk[16]) {
    uint8_t hash[32];
    mbedtls_sha256(reinterpret_cast<const uint8_t*>(kEspNowPmkLabel), strlen(kEspNowPmkLabel), hash, 0);
    memcpy(out_pmk, hash, 16);
}

void pairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, uint8_t out[16]) {
    uint8_t full[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, 16);
    mbedtls_md_hmac_update(&ctx, data, len);
    mbedtls_md_hmac_finish(&ctx, full);
    mbedtls_md_free(&ctx);

    memcpy(out, full, 16);
}

bool verifyPairingAuthTag(const uint8_t key[16], const uint8_t* data, size_t len, const uint8_t tag[16]) {
    uint8_t expected[16];
    pairingAuthTag(key, data, len, expected);
    return memcmp(expected, tag, 16) == 0;
}

bool deriveDeviceCode(char out[9]) {
    uint8_t secret[8];
    bool minted = loadOrCreateDeviceSecret(secret);

    uint8_t hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(kDeviceCodeContext), strlen(kDeviceCodeContext));
    mbedtls_sha256_update(&ctx, secret, sizeof(secret));
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    uint64_t value = ((uint64_t)hash[0] << 32) | ((uint64_t)hash[1] << 24) |
                     ((uint64_t)hash[2] << 16) | ((uint64_t)hash[3] << 8) |
                     (uint64_t)hash[4];
    for (int i = 7; i >= 0; --i) {
        out[i] = kB32Alphabet[value & 31];
        value >>= 5;
    }
    out[8] = '\0';
    return minted;
}

uint32_t randomNonce() {
    uint32_t nonce;
    do {
        nonce = esp_random();
    } while (nonce == 0);
    return nonce;
}

void issueRxChallenge(std::atomic<uint32_t>& rx_nonce) {
    rx_nonce.store(randomNonce(), std::memory_order_release);
}

bool stampAntiReplayTag(std::atomic<bool>& tx_peer_known,
                        std::atomic<uint32_t>& tx_peer_nonce,
                        std::atomic<uint32_t>& tx_counter,
                        uint8_t tag[8]) {
    if (!tx_peer_known.load(std::memory_order_acquire)) {
        return false;
    }
    uint32_t nonce = tx_peer_nonce.load(std::memory_order_acquire);
    uint32_t counter = tx_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    memcpy(tag, &nonce, 4);
    memcpy(tag + 4, &counter, 4);
    return true;
}

bool acceptReplay(std::atomic<uint32_t>& rx_nonce,
                  ReplayState& replay,
                  uint32_t nonce,
                  uint32_t counter,
                  uint32_t now_ms) {
    uint32_t current = rx_nonce.load(std::memory_order_acquire);
    if (current != replay.win_nonce) {
        replay.win_nonce = current;
        replay.win_top = 0;
        replay.win_bitmap = 0;
        replay.underflow = 0;
    }
    if (nonce != current || counter == 0) {
        return false;
    }

    if (counter > replay.win_top) {
        uint32_t shift = counter - replay.win_top;
        replay.win_bitmap = (shift >= 64) ? 0ULL : (replay.win_bitmap << shift);
        replay.win_bitmap |= 1ULL;
        replay.win_top = counter;
        replay.underflow = 0;
        return true;
    }

    uint32_t diff = replay.win_top - counter;
    if (diff < 64) {
        uint64_t mask = 1ULL << diff;
        if (replay.win_bitmap & mask) {
            return false;
        }
        replay.win_bitmap |= mask;
        return true;
    }

    if (++replay.underflow >= kReplayUnderflowLimit) {
        if ((uint32_t)(now_ms - replay.regen_ms) > kReplayRegenMs) {
            replay.regen_ms = now_ms;
            issueRxChallenge(rx_nonce);
        }
        replay.underflow = 0;
    }
    return false;
}

}  // namespace ESPNowCrypto

#endif
#endif
#endif