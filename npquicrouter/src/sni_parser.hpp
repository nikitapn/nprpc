#pragma once
// TLS ClientHello SNI extraction — no heap, no external dependencies.
// Used by both the TCP peek path and the QUIC Initial decrypt path.

#include <cstdint>
#include <span>
#include <string_view>

// Parse the SNI from a TLS ClientHello body.
// `hello` starts at ProtocolVersion (2 bytes), NOT at the Handshake framing header.
// Returns empty on parse failure.
inline std::string_view
sni_from_client_hello_body(std::span<const uint8_t> hello) noexcept
{
  size_t off = 0;

  auto avail  = [&](size_t n) noexcept { return off + n <= hello.size(); };
  auto rd8    = [&]() noexcept -> uint8_t  { return hello[off++]; };
  auto rd16be = [&]() noexcept -> uint16_t {
    const uint16_t v = (uint16_t(hello[off]) << 8) | hello[off + 1];
    off += 2;
    return v;
  };

  // ProtocolVersion (2) + Random (32)
  if (!avail(34)) return {};
  off += 34;

  // SessionID (1-byte length + data)
  if (!avail(1)) return {};
  { const size_t n = rd8(); if (!avail(n)) return {}; off += n; }

  // CipherSuites (2-byte length + data)
  if (!avail(2)) return {};
  { const size_t n = rd16be(); if (!avail(n)) return {}; off += n; }

  // CompressionMethods (1-byte length + data)
  if (!avail(1)) return {};
  { const size_t n = rd8(); if (!avail(n)) return {}; off += n; }

  // Extensions total length — read length first, then compute ext_end from
  // the already-advanced offset (rd16be advances off, then we add ext_len).
  if (!avail(2)) return {};
  const size_t ext_end = [&]{ const size_t n = rd16be(); return off + n; }();

  while (off + 4u <= ext_end && off + 4u <= hello.size()) {
    const uint16_t type = rd16be();
    const uint16_t len  = rd16be();
    if (type == 0x0000u) { // server_name
      // ServerNameList: list_len(2) + name_type(1) + name_len(2) + name
      if (!avail(5u)) return {};
      off += 2u; // skip list_length
      off += 1u; // skip name_type (0x00 = host_name)
      const uint16_t name_len = rd16be();
      if (!avail(name_len)) return {};
      return { reinterpret_cast<const char*>(hello.data() + off), name_len };
    }
    if (!avail(len)) return {};
    off += len;
  }
  return {};
}

// Extract SNI from a raw TLS record on the wire (starts with 0x16 0x03 ...).
// Returns a string_view backed by `data` — valid as long as `data` lives.
//
// NOTE: `data` may be a partial peek buffer smaller than the full TLS record.
// A modern TLS 1.3 ClientHello is 600–900 bytes; the SNI extension is always
// in the first ~200 bytes of extensions, so we work with whatever we have.
// The body parser's avail() checks prevent out-of-bounds access.
inline std::string_view
sni_from_tls_record(std::span<const uint8_t> data) noexcept
{
  // TLS record header: content_type(1) + legacy_version(2) + length(2)
  if (data.size() < 9u) return {};
  if (data[0] != 0x16u) return {}; // must be Handshake (0x16)

  // Don't verify data.size() >= 5 + record_len: the peek buffer may be
  // smaller than the full record. The body parser handles partial data.
  const auto hs = data.subspan(5u);

  // Handshake header: msg_type(1) + length(3) + body
  if (hs.size() < 4u) return {};
  if (hs[0] != 0x01u) return {}; // must be ClientHello

  // Pass whatever we have of the body — avail() guards prevent overrun.
  return sni_from_client_hello_body(hs.subspan(4u));
}

// Extract SNI from a QUIC CRYPTO frame payload.
// In QUIC the CRYPTO frame carries the TLS Handshake message directly
// (no TLS record framing): msg_type(1) + length(3) + ClientHello body.
// Returns a string_view backed by `data`.
inline std::string_view
sni_from_quic_crypto_data(std::span<const uint8_t> data) noexcept
{
  if (data.size() < 4u) return {};
  if (data[0] != 0x01u) return {}; // must be ClientHello

  const size_t hello_len = (size_t(data[1]) << 16) | (size_t(data[2]) << 8) | data[3];
  if (data.size() < 4u + hello_len) return {};

  return sni_from_client_hello_body(data.subspan(4u, hello_len));
}
