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
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/kdf.h>

#include "sni_parser.hpp"

// Controlled by -DQUIC_DEBUG at compile time or quic_debug_enabled at runtime.
// Prints per-packet diagnostics to stderr.
inline bool quic_debug_enabled = false;

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

// ─── CRYPTO fragment returned by quic_extract_crypto ─────────────────────────
struct QuicCryptoFragment {
    std::vector<uint8_t> dcid;          // Destination Connection ID (unencrypted)
    std::vector<uint8_t> scid;          // Source Connection ID (client's own CID)
    uint64_t             stream_offset; // TLS stream byte offset of this fragment
    uint64_t             pkt_num;       // decoded packet number (for building ACKs)
    std::vector<uint8_t> data;          // raw bytes at that offset
    size_t               packet_end;    // bytes consumed from the raw span
};

// ─── Server-side Initial key derivation ───────────────────────────────────────
// Same HKDF-Extract step as client, but uses "server in" expand label.
// dcid = the Destination Connection ID from the client's first Initial packet.
inline std::optional<QuicInitialKeys>
quic_derive_server_initial_keys(const uint8_t* dcid, size_t dcid_len) noexcept
{
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
        if (ok) { size_t len = 32; ok = EVP_PKEY_derive(ctx, initial_secret, &len) > 0; }
        EVP_PKEY_CTX_free(ctx);
        if (!ok) return std::nullopt;
    }
    uint8_t server_secret[32];
    if (!quic_hkdf_expand_label(server_secret, 32, initial_secret, 32, "server in", 9))
        return std::nullopt;
    QuicInitialKeys keys;
    if (!quic_hkdf_expand_label(keys.key.data(), 16, server_secret, 32, "quic key", 8)) return std::nullopt;
    if (!quic_hkdf_expand_label(keys.iv.data(),  12, server_secret, 32, "quic iv",  7)) return std::nullopt;
    if (!quic_hkdf_expand_label(keys.hp.data(),  16, server_secret, 32, "quic hp",  7)) return std::nullopt;
    return keys;
}

// ─── Varint encoder ───────────────────────────────────────────────────────────
inline size_t quic_write_varint(uint8_t* buf, uint64_t v) noexcept
{
    if (v < 0x40u)         { buf[0] = uint8_t(v); return 1; }
    if (v < 0x4000u)       { buf[0] = 0x40u | uint8_t(v >> 8); buf[1] = uint8_t(v); return 2; }
    if (v < 0x40000000u)   {
        buf[0] = 0x80u | uint8_t(v >> 24); buf[1] = uint8_t(v >> 16);
        buf[2] = uint8_t(v >> 8);          buf[3] = uint8_t(v); return 4;
    }
    buf[0] = 0xc0u | uint8_t(v >> 56); buf[1] = uint8_t(v >> 48);
    buf[2] = uint8_t(v >> 40);         buf[3] = uint8_t(v >> 32);
    buf[4] = uint8_t(v >> 24);         buf[5] = uint8_t(v >> 16);
    buf[6] = uint8_t(v >> 8);          buf[7] = uint8_t(v); return 8;
}

// ─── Build a server Initial ACK packet ───────────────────────────────────────
// Sends back an ACK-only QUIC v1 Initial packet so curl's PTO timer is satisfied
// and it continues sending CRYPTO frames.
//
// initial_dcid   = client's original DCID (used for key derivation)
// response_dcid  = DCID to use in our header (= client's SCID)
// largest_acked  = highest packet number we have received from the client
// server_pkt_num = our outgoing packet number counter (caller increments)
inline std::vector<uint8_t>
quic_build_server_ack(const std::vector<uint8_t>& initial_dcid,
                      const std::vector<uint8_t>& response_dcid,
                      uint64_t largest_acked,
                      uint64_t server_pkt_num) noexcept
{
    if (initial_dcid.empty() || response_dcid.empty() || response_dcid.size() > 20u)
        return {};

    const auto keys = quic_derive_server_initial_keys(
        initial_dcid.data(), initial_dcid.size());
    if (!keys) return {};

    // ── Build ACK frame plaintext ─────────────────────────────────────────────
    // type(1) + largest_acked(varint) + ack_delay(1) + range_count(1) + first_range(1)
    uint8_t ack_buf[1 + 8 + 3];
    size_t  ack_len = 0;
    ack_buf[ack_len++] = 0x02u; // ACK frame type
    ack_len += quic_write_varint(ack_buf + ack_len, largest_acked);
    ack_buf[ack_len++] = 0x00u; // ACK Delay = 0
    ack_buf[ack_len++] = 0x00u; // ACK Range Count = 0
    ack_buf[ack_len++] = 0x00u; // First ACK Range = 0 (acks just largest_acked)

    // ── Compute sizes ─────────────────────────────────────────────────────────
    const size_t pn_len     = (server_pkt_num < 256u) ? 1u
                            : (server_pkt_num < 65536u) ? 2u : 4u;
    const size_t payload_len = pn_len + ack_len + 16u; // pn + data + GCM tag

    // ── Build long-header ─────────────────────────────────────────────────────
    // 0xc0 | (pn_len-1): long hdr, fixed bit, Initial type, reserved=0, pn_len
    uint8_t hdr[1 + 4 + 1 + 20 + 1 + 1 + 2 + 4]; // generous upper bound
    size_t  hoff = 0;
    hdr[hoff++] = uint8_t(0xc0u | (pn_len - 1u));
    hdr[hoff++] = 0x00u; hdr[hoff++] = 0x00u;     // QUIC version
    hdr[hoff++] = 0x00u; hdr[hoff++] = 0x01u;
    hdr[hoff++] = uint8_t(response_dcid.size());
    std::memcpy(hdr + hoff, response_dcid.data(), response_dcid.size());
    hoff += response_dcid.size();
    hdr[hoff++] = 0x00u; // SCID length = 0
    hdr[hoff++] = 0x00u; // token length = 0 (1-byte varint)
    hoff += quic_write_varint(hdr + hoff, payload_len);
    const size_t pn_off = hoff;
    // Packet number (big-endian, truncated to pn_len bytes)
    for (size_t i = pn_len; i > 0; --i)
        hdr[hoff++] = uint8_t(server_pkt_num >> (8u * (i - 1u)));
    const size_t aad_len = hoff; // header + pkt_num = AAD

    // Minimum ciphertext+tag must cover the HP sample window:
    // sample = pkt[pn_off+4 .. pn_off+19].  With pn_len=1 and ack_len≤12 we
    // need at least pn_off+20 bytes total, i.e. ciphertext+tag ≥ 19 bytes.
    // ack_len(≤12) + 16(tag) = ≤28 ≥ 19: always satisfied.

    // ── Encrypt (AES-128-GCM) ─────────────────────────────────────────────────
    std::array<uint8_t, 12> nonce = keys->iv;
    for (size_t i = 0; i < 8u; ++i)
        nonce[11u - i] ^= uint8_t(server_pkt_num >> (8u * i));

    std::vector<uint8_t> pkt(aad_len + ack_len + 16u);
    std::memcpy(pkt.data(), hdr, aad_len);
    uint8_t* ct  = pkt.data() + aad_len;
    uint8_t* tag = ct + ack_len;
    {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return {};
        int outl = 0, outl2 = 0;
        const bool ok =
            EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), nullptr, nullptr, nullptr) > 0 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) > 0 &&
            EVP_EncryptInit_ex(ctx, nullptr, nullptr, keys->key.data(), nonce.data()) > 0 &&
            EVP_EncryptUpdate(ctx, nullptr, &outl, pkt.data(), (int)aad_len) > 0 && // AAD
            EVP_EncryptUpdate(ctx, ct, &outl, ack_buf, (int)ack_len) > 0 &&
            EVP_EncryptFinal_ex(ctx, ct + outl, &outl2) > 0 &&
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) > 0;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) return {};
    }

    // ── Apply header protection ───────────────────────────────────────────────
    // sample = pkt[pn_off+4 .. pn_off+19]
    {
        uint8_t mask[16]{};
        int outl = 0;
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return {};
        const bool ok =
            EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, keys->hp.data(), nullptr) > 0 &&
            EVP_CIPHER_CTX_set_padding(ctx, 0) > 0 &&
            EVP_EncryptUpdate(ctx, mask, &outl, pkt.data() + pn_off + 4u, 16) > 0;
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) return {};
        pkt[0] ^= mask[0] & 0x0fu;
        for (size_t i = 0; i < pn_len; ++i)
            pkt[pn_off + i] ^= mask[1u + i];
    }

    if (quic_debug_enabled)
        std::cerr << "[QUIC] sent server ACK pkt_num=" << server_pkt_num
                  << " ack=" << largest_acked << " size=" << pkt.size() << "\n";
    return pkt;
}



// Compute the total byte length of a QUIC v1 Initial long-header packet
// starting at raw[0] WITHOUT decrypting it. Returns 0 on any parse error.
// Used to skip over one coalesced packet to reach the next.
inline size_t
quic_initial_packet_len(std::span<const uint8_t> raw) noexcept
{
    if (raw.size() < 7u) return 0;
    if ((raw[0] & 0xf0u) != 0xc0u) return 0;
    const uint32_t version = (uint32_t(raw[1]) << 24) | (uint32_t(raw[2]) << 16)
                           | (uint32_t(raw[3]) <<  8) | raw[4];
    if (version != k_quic_v1) return 0;
    size_t off = 5u;
    if (off >= raw.size()) return 0;
    const size_t dcid_len = raw[off++];
    if (off + dcid_len > raw.size()) return 0;
    off += dcid_len;
    if (off >= raw.size()) return 0;
    const size_t scid_len = raw[off++];
    if (off + scid_len > raw.size()) return 0;
    off += scid_len;
    auto tv = quic_varint(raw, off);
    if (!tv) return 0;
    off += tv->second + (size_t)tv->first;
    if (off > raw.size()) return 0;
    auto lv = quic_varint(raw, off);
    if (!lv) return 0;
    const size_t pkt_end = off + lv->second + (size_t)lv->first;
    return pkt_end; // caller bounds-checks against datagram size
}

// Extract ALL CRYPTO frames from a QUIC v1 Initial packet.
// Returns an empty vector if: not QUIC v1 Initial, decryption fails.
// A single QUIC packet can contain multiple CRYPTO frames with different offsets
// (quiche/curl sends them in arbitrary order, not necessarily offset-ascending).
inline std::vector<QuicCryptoFragment>
quic_extract_crypto(std::span<const uint8_t> raw)
{
#define QUIC_DBG(msg) do { if (quic_debug_enabled) std::cerr << "[QUIC] " << msg << "\n"; } while(0)

    // Byte 0: 1(header_form) 1(fixed) 00(initial type) xx(reserved) xx(pn_len)
    if (raw.size() < 7u) { QUIC_DBG("too short: " << raw.size()); return {}; }
    if ((raw[0] & 0xf0u) != 0xc0u) {
        QUIC_DBG("not QUIC Initial: first_byte=0x" << std::hex << (int)raw[0]);
        return {};
    }

    // Version
    const uint32_t version = (uint32_t(raw[1]) << 24) | (uint32_t(raw[2]) << 16)
                            | (uint32_t(raw[3]) <<  8) | raw[4];
    if (version != k_quic_v1) {
        QUIC_DBG("unsupported version: 0x" << std::hex << version);
        return {};
    }

    size_t off = 5u;

    // DCID
    if (off >= raw.size()) { QUIC_DBG("DCID len byte OOB"); return {}; }
    const size_t dcid_len = raw[off++];
    if (off + dcid_len > raw.size()) { QUIC_DBG("DCID OOB (dcid_len=" << dcid_len << ")"); return {}; }
    const uint8_t* dcid = raw.data() + off;
    off += dcid_len;

    // SCID
    if (off >= raw.size()) { QUIC_DBG("SCID len byte OOB"); return {}; }
    const size_t scid_len = raw[off++];
    if (off + scid_len > raw.size()) { QUIC_DBG("SCID OOB (scid_len=" << scid_len << ")"); return {}; }
    std::vector<uint8_t> scid_vec(raw.data() + off, raw.data() + off + scid_len);
    off += scid_len;

    // Token (Initial-only varint-length field)
    auto tv = quic_varint(raw, off);
    if (!tv) { QUIC_DBG("token length varint failed at off=" << off); return {}; }
    off += tv->second + (size_t)tv->first; // advance past length + token bytes
    if (off > raw.size()) { QUIC_DBG("token data OOB (token_len=" << tv->first << ")"); return {}; }

    // Payload length (includes pkt_num + ciphertext + 16-byte GCM tag)
    auto lv = quic_varint(raw, off);
    if (!lv) { QUIC_DBG("payload length varint failed at off=" << off); return {}; }
    off += lv->second;
    const size_t payload_len = (size_t)lv->first;
    const size_t pn_off = off;
    const size_t packet_end = pn_off + payload_len;
    if (packet_end > raw.size()) { QUIC_DBG("payload OOB (payload_len=" << payload_len << " pn_off=" << pn_off << " raw=" << raw.size() << ")"); return {}; }
    if (payload_len < 21u) { QUIC_DBG("payload too small: " << payload_len); return {}; }

    // Derive keys
    const auto keys = quic_derive_initial_keys(dcid, dcid_len);
    if (!keys) { QUIC_DBG("key derivation failed (dcid_len=" << dcid_len << ")"); return {}; }

    // Remember DCID for the caller to use as a reassembly key.
    std::vector<uint8_t> dcid_vec(dcid, dcid + dcid_len);

    // Only decrypt up to packet_end (ignore any trailing coalesced packets).
    std::vector<uint8_t> pkt(raw.begin(), raw.begin() + (ptrdiff_t)packet_end);
    const size_t pn_len = quic_remove_header_protection(pkt, pn_off, keys->hp);
    if (pn_len == 0) { QUIC_DBG("HP removal failed (pn_off=" << pn_off << " pkt_size=" << pkt.size() << ")"); return {}; }

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
        if (!ok) {
            QUIC_DBG("AES-128-GCM decryption failed (pkt_num=" << pkt_num
                     << " aad_len=" << aad_len << " data_len=" << data_len << ")");
            return {};
        }
    }

    QUIC_DBG("decryption ok, plaintext_len=" << plaintext.size());

    // Walk ALL QUIC frames, collecting every CRYPTO frame.
    // RFC 9000 §17.2.2: Initial packets may contain PADDING, PING, ACK, CRYPTO,
    // and CONNECTION_CLOSE frames — in any order.
    // IMPORTANT: quiche/curl sends CRYPTO frames in non-ascending offset order
    // within a single packet, so we must not stop at the first CRYPTO frame.
    std::vector<QuicCryptoFragment> result;
    size_t foff = 0;
    const auto sp = std::span<const uint8_t>(plaintext);
    while (foff < plaintext.size()) {
        const uint8_t ftype = plaintext[foff++];
        if (ftype == 0x00u) continue; // PADDING
        if (ftype == 0x01u) continue; // PING
        if (ftype == 0x02u || ftype == 0x03u) { // ACK (0x02) / ACK+ECN (0x03)
            uint64_t range_count = 0;
            for (int i = 0; i < 4; ++i) {
                auto v = quic_varint(sp, foff);
                if (!v) { QUIC_DBG("ACK field " << i << " parse failed"); return result; }
                if (i == 2) range_count = v->first;
                foff += v->second;
            }
            for (uint64_t i = 0; i < range_count; ++i) {
                for (int j = 0; j < 2; ++j) {
                    auto v = quic_varint(sp, foff);
                    if (!v) { QUIC_DBG("ACK range parse failed"); return result; }
                    foff += v->second;
                }
            }
            if (ftype == 0x03u) {
                for (int i = 0; i < 3; ++i) {
                    auto v = quic_varint(sp, foff);
                    if (!v) { QUIC_DBG("ECN count parse failed"); return result; }
                    foff += v->second;
                }
            }
            continue;
        }
        if (ftype == 0x06u) { // CRYPTO
            auto cv = quic_varint(sp, foff);
            if (!cv) { QUIC_DBG("CRYPTO offset varint failed"); return result; }
            const uint64_t crypto_stream_off = cv->first;
            foff += cv->second;

            auto dv = quic_varint(sp, foff);
            if (!dv) { QUIC_DBG("CRYPTO length varint failed"); return result; }
            foff += dv->second;
            const size_t crypto_len = (size_t)dv->first;

            const size_t available = (foff <= plaintext.size()) ? plaintext.size() - foff : 0u;
            const size_t take = std::min(crypto_len, available);
            if (take == 0) { foff += crypto_len; continue; } // skip empty/oob frame

            QUIC_DBG("CRYPTO frag stream_off=" << crypto_stream_off
                     << " len=" << take
                     << (take < crypto_len ? " [partial]" : " [complete]"));

            QuicCryptoFragment frag;
            frag.dcid          = dcid_vec;
            frag.scid          = scid_vec;
            frag.stream_offset = crypto_stream_off;
            frag.pkt_num       = pkt_num;
            frag.packet_end    = packet_end;
            frag.data.assign(plaintext.begin() + (ptrdiff_t)foff,
                             plaintext.begin() + (ptrdiff_t)(foff + take));
            result.push_back(std::move(frag));
            foff += take;
            continue;
        }
        QUIC_DBG("unknown/terminal frame type 0x" << std::hex << (int)ftype
                 << " at plaintext_off=" << std::dec << (foff-1)
                 << " — stopping frame walk");
        break;
    }
    if (result.empty()) QUIC_DBG("no CRYPTO frames found in plaintext");
    return result;
#undef QUIC_DBG
}

// Convenience: extract SNI from a single self-contained QUIC Initial datagram.
// Works only when the full ClientHello fits in one packet.
// For multi-packet ClientHellos use quic_extract_crypto() + reassembly instead.
inline std::string
sni_from_quic_initial(std::span<const uint8_t> raw)
{
    auto frags = quic_extract_crypto(raw);
    if (frags.empty()) return {};
    const auto sv = sni_from_quic_crypto_data(
        std::span<const uint8_t>(frags[0].data.data(), frags[0].data.size()));
    return std::string(sv);
}
