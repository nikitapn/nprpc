// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace nprpc::impl {

/// HTTP content-codings the server can produce (RFC 9110 §8.4.1).
enum class ContentEncoding : uint8_t {
  Identity = 0,
  Gzip = 1,    // RFC 1952 container
  Deflate = 2, // RFC 1950 (zlib) container, which is what "deflate" means in HTTP
  Brotli = 3,  // RFC 7932, token "br"
};

inline constexpr size_t k_compressed_encoding_count = 3;

/// Index into per-encoding storage; only valid for the non-identity codings.
constexpr size_t content_encoding_index(ContentEncoding e) noexcept
{
  return static_cast<size_t>(e) - 1;
}

/// The token used in Content-Encoding / Accept-Encoding.
std::string_view content_encoding_token(ContentEncoding e) noexcept;

/// Pick the coding to answer an Accept-Encoding header with.
/// Honours q-values (q=0 forbids a coding) and "*"; on a tie prefers br, then
/// gzip, then deflate. Returns Identity when the header is absent or nothing
/// we can produce is acceptable.
ContentEncoding negotiate_content_encoding(std::string_view accept_encoding) noexcept;

/// Whether a response of this media type is worth compressing: text, and the
/// structured text formats (JS, JSON, XML, SVG, WASM, ...). Already-compressed
/// formats (images, video, woff2, archives) are not.
bool is_compressible_content_type(std::string_view content_type) noexcept;

/// Compression level for a response compressed per request (page handlers):
/// zlib's default, and a brotli quality that costs about what gzip does.
inline constexpr int k_dynamic_compression_level = -1;

/// The level a static file of @p size bytes is compressed with, once, for the
/// file cache. zlib's default for gzip/deflate; for brotli the best quality
/// (11) while it stays cheap enough to build on a first request, lower above
/// that so a large asset never stalls a worker for seconds.
int static_compression_level(ContentEncoding encoding, size_t size) noexcept;

/// Compress @p len bytes at @p data with the given coding.
/// @p level is the zlib level (0-9) for gzip/deflate and the quality (0-11)
/// for brotli; -1 picks each coding's per-request default.
/// Returns std::nullopt on an encoder error or for ContentEncoding::Identity.
std::optional<std::vector<uint8_t>> compress_body(
    const uint8_t* data,
    size_t len,
    ContentEncoding encoding,
    int level = k_dynamic_compression_level);

} // namespace nprpc::impl
