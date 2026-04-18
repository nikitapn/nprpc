#pragma once
// QUIC v1 Initial packet key derivation and decryption.
// Used to extract the TLS ClientHello → SNI from the first UDP datagram.
//
// RFC 9001 §5: Initial packets use well-known keys derived from the DCID,
// so any observer can decrypt them — the keys are not secret.
// No ngtcp2 dependency: uses OpenSSL EVP directly.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include "sni_parser.hpp"

// ─── QUIC v1 constants ────────────────────────────────────────────────────────
static constexpr uint32_t k_quic_v1 = 0x00000001u;

// Initial salt (RFC 9001 §5.2, version 1)
static constexpr uint8_t k_quic_v1_initial_salt[20] = {
    0x38, 0x76, 0x2c, 0xf7, 0xf5, 0x59, 0x34, 0xb3, 0x4d, 0x17,
    0x9a, 0xe6, 0xa4, 0xc8, 0x0c, 0xad, 0xcc, 0xbb, 0x7f, 0x0a
};

// ─── QUIC variable-length integer ─────────────────────────────────────────────
// Returns {value, bytes_consumed} or nullopt if out of bounds.
inline std::optional<std::pair<uint64_t, size_t>>
quic_varint(std::span<const uint8_t> buf, size_t off) noexcept
{
    if (off >= buf.size()) return std::nullopt;
    const uint8_t prefix = buf[off] >> 6;
    const size_t  nbytes = size_t{1} << prefix;
    if (off + nbytes > buf.size()) return std::nullopt;
    uint64_t val = buf[off] & 0x3fu;
    for (size_t i = 1; i < nbytes; ++i)
        val = (val << 8) | buf[off + i];
    return std::pair{val, nbytes};
}

// ─── HKDF-Expand-Label (RFC 8446 §7.1) ────────────────────────────────────────
// label_prefix "tls13 " is prepended automatically.
inline bool
quic_hkdf_expand_label(uint8_t* out, size_t out_len,
                       const uint8_t* secret, size_t secret_len,
                       const char* label, size_t label_len) noexcept
{
    // HkdfLabel wire format:
    //   length(2BE) | uint8(len("tls13 " + label)) | "tls13 " | label | uint8(0)
    static constexpr char kPrefix[]  = "tls13 ";
    static constexpr size_t kPfxLen  = 6u;

    uint8_t info[2u + 1u + kPfxLen + 64u + 1u];
    size_t  ioff = 0;
    info[ioff++] = uint8_t(out_len >> 8);
    info[ioff++] = uint8_t(out_len);
    info[ioff++] = uint8_t(kPfxLen + label_len);
    std::memcpy(info + ioff, kPrefix, kPfxLen); ioff += kPfxLen;
    std::memcpy(info + ioff, label,   label_len); ioff += label_len;
    info[ioff++] = 0u; // context length = 0

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!ctx) return false;

    bool ok = (EVP_PKEY_derive_init(ctx) > 0) &&
              (EVP_PKEY_CTX_hkdf_mode(ctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) > 0) &&
              (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0) &&
              (EVP_PKEY_CTX_set1_hkdf_key(ctx, secret, (int)secret_len) > 0) &&
              (EVP_PKEY_CTX_add1_hkdf_info(ctx, info, (int)ioff) > 0);
    if (ok) {
        size_t len = out_len;
        ok = EVP_PKEY_derive(ctx, out, &len) > 0 && len == out_len;
    }
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

// ─── Per-connection Initial key material ─────────────────────────────────────
struct QuicInitialKeys {
    std::array<uint8_t, 16> key; // AES-128-GCM encryption key
    std::array<uint8_t, 12> iv;  // AEAD nonce base
    std::array<uint8_t, 16> hp;  // header protection key
};

// Derive QUIC v1 *client* Initial keys from the Destination Connection ID.
inline std::optional<QuicInitialKeys>
quic_derive_initial_keys(const uint8_t* dcid, size_t dcid_len) noexcept
{
    // initial_secret = HKDF-Extract(salt=initial_salt, IKM=dcid)
    uint8_t initial_secret[32];
    {
        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
        if (!ctx) return std::nullopt;
        bool ok = (EVP_PKEY_derive_init(ctx) > 0) &&
                  (EVP_PKEY_CTX_hkdf_mode(ctx, EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY) > 0) &&
                  (EVP_PKEY_CTX_set_hkdf_md(ctx, EVP_sha256()) > 0) &&
                  (EVP_PKEY_CTX_set1_hkdf_salt(ctx, k_quic_v1_initial_salt,
                                                sizeof(k_quic_v1_initial_salt)) > 0) &&
                  (EVP_PKEY_CTX_set1_hkdf_key(ctx, dcid, (int)dcid_len) > 0);
        if (ok) {
            size_t len = 32;
            ok = EVP_PKEY_derive(ctx, initial_secret, &len) > 0;
        }
        EVP_PKEY_CTX_free(ctx);
        if (!ok) return std::nullopt;
    }

    // client_secret = HKDF-Expand-Label(initial_secret, "client in", "", 32)
    uint8_t client_secret[32];
    if (!quic_hkdf_expand_label(client_secret, 32,
                                initial_secret, 32, "client in", 9))
        return std::nullopt;

    QuicInitialKeys keys;
    if (!quic_hkdf_expand_label(keys.key.data(), 16, client_secret, 32, "quic key", 8))
        return std::nullopt;
    if (!quic_hkdf_expand_label(keys.iv.data(),  12, client_secret, 32, "quic iv",  7))
        return std::nullopt;
    if (!quic_hkdf_expand_label(keys.hp.data(),  16, client_secret, 32, "quic hp",  7))
        return std::nullopt;

    return keys;
}

// ─── AES-128 header protection removal ───────────────────────────────────────
// Removes AES-128-ECB header protection from a mutable packet buffer.
// `pn_off` is the byte offset of the packet number field.
// Returns pn_len (1-4) on success, 0 on failure.
inline size_t
quic_remove_header_protection(std::vector<uint8_t>& pkt, size_t pn_off,
                               const std::array<uint8_t, 16>& hp) noexcept
{
    // sample = pkt[pn_off+4 .. pn_off+20]  (RFC 9001 §5.4.2)
    if (pn_off + 20u > pkt.size()) return 0;

    uint8_t mask[32];
    int outl = 0;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;
    const bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, hp.data(), nullptr) > 0 &&
        EVP_CIPHER_CTX_set_padding(ctx, 0) > 0 &&
        EVP_EncryptUpdate(ctx, mask, &outl, pkt.data() + pn_off + 4u, 16) > 0 &&
        outl == 16;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) return 0;

    // Unmask first byte — long header: lower 4 bits (reserved(2) + pkt_num_len(2))
    pkt[0] ^= mask[0] & 0x0fu;
    const size_t pn_len = (pkt[0] & 0x03u) + 1u;
    for (size_t i = 0; i < pn_len; ++i)
        pkt[pn_off + i] ^= mask[1u + i];

    return pn_len;
}

// ─── Top-level: extract SNI from a raw QUIC Initial UDP datagram ─────────────
// Returns empty string on failure (not QUIC v1 Initial, decryption error, no SNI).
inline std::string
sni_from_quic_initial(std::span<const uint8_t> raw)
{
    // Byte 0: 1(header_form) 1(fixed) 00(initial type) xx(reserved) xx(pn_len)
    if (raw.size() < 7u) return {};
    if ((raw[0] & 0xf0u) != 0xc0u) return {}; // must be Long Header + Initial

    // Version
    const uint32_t version = (uint32_t(raw[1]) << 24) | (uint32_t(raw[2]) << 16)
                            | (uint32_t(raw[3]) <<  8) | raw[4];
    if (version != k_quic_v1) return {};

    size_t off = 5u;

    // DCID
    if (off >= raw.size()) return {};
    const size_t dcid_len = raw[off++];
    if (off + dcid_len > raw.size()) return {};
    const uint8_t* dcid = raw.data() + off;
    off += dcid_len;

    // SCID
    if (off >= raw.size()) return {};
    const size_t scid_len = raw[off++];
    if (off + scid_len > raw.size()) return {};
    off += scid_len;

    // Token (Initial-only varint-length field)
    auto tv = quic_varint(raw, off);
    if (!tv) return {};
    off += tv->second + (size_t)tv->first; // advance past length + token bytes
    if (off > raw.size()) return {};

    // Payload length (includes pkt_num + ciphertext + 16-byte GCM tag)
    auto lv = quic_varint(raw, off);
    if (!lv) return {};
    off += lv->second;
    const size_t payload_len = (size_t)lv->first;
    const size_t pn_off = off;
    if (pn_off + payload_len > raw.size()) return {};
    if (payload_len < 21u) return {}; // min: pn(1) + sample_skip(4) + sample(16)

    // Derive keys
    const auto keys = quic_derive_initial_keys(dcid, dcid_len);
    if (!keys) return {};

    // Copy to mutable buffer for HP removal
    std::vector<uint8_t> pkt(raw.begin(), raw.end());
    const size_t pn_len = quic_remove_header_protection(pkt, pn_off, keys->hp);
    if (pn_len == 0) return {};

    // Reconstruct truncated packet number (Initial packets are always near 0)
    uint64_t pkt_num = 0;
    for (size_t i = 0; i < pn_len; ++i)
        pkt_num = (pkt_num << 8) | pkt[pn_off + i];

    // Nonce = iv XOR right-aligned pkt_num
    std::array<uint8_t, 12> nonce = keys->iv;
    for (size_t i = 0; i < 8u; ++i)
        nonce[11u - i] ^= uint8_t(pkt_num >> (8u * i));

    // AAD = all header bytes including packet number
    const size_t aad_len = pn_off + pn_len;

    // Ciphertext layout: pkt[pn_off+pn_len .. pn_off+payload_len-16] + tag(16)
    const size_t ct_start = pn_off + pn_len;
    if (payload_len < pn_len + 16u) return {};
    const size_t data_len = payload_len - pn_len - 16u;
    const uint8_t* ciphertext = pkt.data() + ct_start;
    const uint8_t* tag        = ciphertext + data_len;

    // AES-128-GCM decrypt
    std::vector<uint8_t> plaintext(data_len);
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return {};
        int outl = 0, outl2 = 0;
        const bool ok =
            EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) > 0 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) > 0 &&
            EVP_DecryptInit_ex(ctx, nullptr, nullptr, keys->key.data(), nonce.data()) > 0 &&
            EVP_DecryptUpdate(ctx, nullptr, &outl, pkt.data(), (int)aad_len) > 0 &&
            EVP_DecryptUpdate(ctx, plaintext.data(), &outl,
                              ciphertext, (int)data_len) > 0 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                 const_cast<uint8_t*>(tag)) > 0 &&
            EVP_DecryptFinal_ex(ctx, plaintext.data() + outl, &outl2) > 0;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) return {};
    }

    // Walk QUIC frames looking for CRYPTO (0x06)
    size_t foff = 0;
    const auto sp = std::span<const uint8_t>(plaintext);
    while (foff < plaintext.size()) {
        const uint8_t ftype = plaintext[foff++];
        if (ftype == 0x00u) continue; // PADDING
        if (ftype == 0x01u) continue; // PING
        if (ftype == 0x06u) {         // CRYPTO
            // crypto_offset (varint) — skip it
            auto cv = quic_varint(sp, foff);
            if (!cv) return {};
            foff += cv->second;
            // data_length (varint)
            auto dv = quic_varint(sp, foff);
            if (!dv) return {};
            foff += dv->second;
            const size_t crypto_len = (size_t)dv->first;
            if (foff + crypto_len > plaintext.size()) return {};
            const auto sni_sv = sni_from_quic_crypto_data(
                sp.subspan(foff, crypto_len));
            return std::string(sni_sv);
        }
        break; // unknown frame — stop scanning
    }
    return {};
}
