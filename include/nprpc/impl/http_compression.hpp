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
};

inline constexpr size_t k_compressed_encoding_count = 2;

/// Index into per-encoding storage; only valid for Gzip and Deflate.
constexpr size_t content_encoding_index(ContentEncoding e) noexcept
{
  return static_cast<size_t>(e) - 1;
}

/// The token used in Content-Encoding / Accept-Encoding.
std::string_view content_encoding_token(ContentEncoding e) noexcept;

/// Pick the coding to answer an Accept-Encoding header with.
/// Honours q-values (q=0 forbids a coding) and "*"; prefers gzip over deflate
/// on a tie. Returns Identity when the header is absent or nothing we can
/// produce is acceptable.
ContentEncoding negotiate_content_encoding(std::string_view accept_encoding) noexcept;

/// Whether a response of this media type is worth compressing: text, and the
/// structured text formats (JS, JSON, XML, SVG, WASM, ...). Already-compressed
/// formats (images, video, woff2, archives) are not.
bool is_compressible_content_type(std::string_view content_type) noexcept;

/// Compress @p len bytes at @p data with the given coding.
/// Returns std::nullopt on a zlib error or for ContentEncoding::Identity.
std::optional<std::vector<uint8_t>> compress_body(const uint8_t* data,
                                                  size_t len,
                                                  ContentEncoding encoding,
                                                  int level = -1);

} // namespace nprpc::impl
