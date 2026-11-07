// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/impl/http_compression.hpp>

#include <algorithm>
#include <limits>

#include <brotli/encode.h>
#include <zlib.h>

namespace nprpc::impl {

namespace {

constexpr char ascii_lower(char c) noexcept
{
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool iequals(std::string_view a, std::string_view b) noexcept
{
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return ascii_lower(x) == ascii_lower(y); });
}

bool istarts_with(std::string_view s, std::string_view prefix) noexcept
{
  return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view s, std::string_view suffix) noexcept
{
  return s.size() >= suffix.size() &&
         iequals(s.substr(s.size() - suffix.size()), suffix);
}

std::string_view trim(std::string_view s) noexcept
{
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

// qvalue = ( "0" [ "." 0*3DIGIT ] ) / ( "1" [ "." 0*3("0") ] ), in thousandths.
// Anything unparsable is treated as q=1, the value the parameter defaults to.
int parse_qvalue(std::string_view v) noexcept
{
  v = trim(v);
  if (v.empty() || (v[0] != '0' && v[0] != '1')) return 1000;
  int q = (v[0] - '0') * 1000;
  if (v.size() > 1 && v[1] == '.') {
    int scale = 100;
    for (size_t i = 2; i < v.size() && i < 5; ++i) {
      if (v[i] < '0' || v[i] > '9') break;
      q += (v[i] - '0') * scale;
      scale /= 10;
    }
  }
  return std::min(q, 1000);
}

} // namespace

std::string_view content_encoding_token(ContentEncoding e) noexcept
{
  switch (e) {
  case ContentEncoding::Gzip:
    return "gzip";
  case ContentEncoding::Deflate:
    return "deflate";
  case ContentEncoding::Brotli:
    return "br";
  case ContentEncoding::Identity:
    break;
  }
  return "identity";
}

ContentEncoding negotiate_content_encoding(std::string_view accept_encoding) noexcept
{
  // -1 = not mentioned
  int q_br = -1;
  int q_gzip = -1;
  int q_deflate = -1;
  int q_any = -1;

  while (!accept_encoding.empty()) {
    const auto comma = accept_encoding.find(',');
    auto element = accept_encoding.substr(0, comma);
    accept_encoding.remove_prefix(comma == std::string_view::npos
                                      ? accept_encoding.size()
                                      : comma + 1);

    const auto semi = element.find(';');
    const auto coding = trim(element.substr(0, semi));
    int q = 1000;
    if (semi != std::string_view::npos) {
      auto params = element.substr(semi + 1);
      while (!params.empty()) {
        const auto next = params.find(';');
        const auto param = trim(params.substr(0, next));
        params.remove_prefix(next == std::string_view::npos ? params.size()
                                                            : next + 1);
        if (param.size() >= 2 && ascii_lower(param[0]) == 'q' && param[1] == '=') {
          q = parse_qvalue(param.substr(2));
        }
      }
    }

    if (iequals(coding, "br")) {
      q_br = std::max(q_br, q);
    } else if (iequals(coding, "gzip") || iequals(coding, "x-gzip")) {
      q_gzip = std::max(q_gzip, q);
    } else if (iequals(coding, "deflate")) {
      q_deflate = std::max(q_deflate, q);
    } else if (coding == "*") {
      q_any = std::max(q_any, q);
    }
  }

  const auto effective = [q_any](int q) { return q >= 0 ? q : std::max(q_any, 0); };
  const int br = effective(q_br);
  const int gzip = effective(q_gzip);
  const int deflate = effective(q_deflate);

  // Highest q wins; ties go to the better coding.
  if (br > 0 && br >= gzip && br >= deflate) return ContentEncoding::Brotli;
  if (gzip > 0 && gzip >= deflate) return ContentEncoding::Gzip;
  if (deflate > 0) return ContentEncoding::Deflate;
  return ContentEncoding::Identity;
}

bool is_compressible_content_type(std::string_view content_type) noexcept
{
  auto type = trim(content_type.substr(0, content_type.find(';')));
  if (type.empty()) return false;

  if (istarts_with(type, "text/")) return true;
  if (iends_with(type, "+json") || iends_with(type, "+xml")) return true;

  static constexpr std::string_view k_types[] = {
      "application/javascript",
      "application/x-javascript",
      "application/ecmascript",
      "application/json",
      "application/xml",
      "application/wasm",
      "application/x-font-ttf",
      "application/vnd.ms-fontobject",
      "font/ttf",
      "font/otf",
      "image/bmp",
      "image/vnd.microsoft.icon",
      "image/x-icon",
  };
  for (auto t : k_types) {
    if (iequals(type, t)) return true;
  }
  return false;
}

namespace {

// Quality 5 compresses about as fast as gzip's default and still smaller.
constexpr int k_brotli_dynamic_quality = 5;

std::optional<std::vector<uint8_t>> compress_brotli(const uint8_t* data,
                                                    size_t len,
                                                    int quality)
{
  if (quality < BROTLI_MIN_QUALITY || quality > BROTLI_MAX_QUALITY)
    quality = k_brotli_dynamic_quality;

  size_t out_len = BrotliEncoderMaxCompressedSize(len);
  if (out_len == 0) return std::nullopt; // input too large to bound
  std::vector<uint8_t> out(out_len);
  // Text mode tunes the context modelling for UTF-8; everything we compress
  // is text or text-like.
  if (!BrotliEncoderCompress(quality, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_TEXT,
                             len, data, &out_len, out.data())) {
    return std::nullopt;
  }
  out.resize(out_len);
  return out;
}

} // namespace

int static_compression_level(ContentEncoding encoding, size_t size) noexcept
{
  if (encoding != ContentEncoding::Brotli) return -1;
  // Measured on minified JS: quality 11 runs at ~0.7 MB/s, 9 at ~17 MB/s,
  // 6 at ~33 MB/s. The copy is built on the first request for the file, so
  // keep that to ~100 ms; 11 gains only a few percent over 9 anyway.
  if (size <= 64 * 1024) return BROTLI_MAX_QUALITY;
  if (size <= 1024 * 1024) return 9;
  return 6;
}

std::optional<std::vector<uint8_t>> compress_body(const uint8_t* data,
                                                  size_t len,
                                                  ContentEncoding encoding,
                                                  int level)
{
  if (encoding == ContentEncoding::Identity) return std::nullopt;
  if (encoding == ContentEncoding::Brotli)
    return compress_brotli(data, len, level);
  // zlib counts in uInt; bodies this large are never worth compressing inline.
  if (len > std::numeric_limits<uInt>::max() / 2) return std::nullopt;

  // windowBits 15 writes a zlib stream; +16 wraps the same stream in gzip.
  const int window_bits = encoding == ContentEncoding::Gzip ? 15 + 16 : 15;

  z_stream zs{};
  if (deflateInit2(&zs, level, Z_DEFLATED, window_bits, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK) {
    return std::nullopt;
  }

  std::vector<uint8_t> out(deflateBound(&zs, static_cast<uLong>(len)));
  zs.next_in = const_cast<Bytef*>(data);
  zs.avail_in = static_cast<uInt>(len);
  zs.next_out = out.data();
  zs.avail_out = static_cast<uInt>(out.size());

  // deflateBound() sized the output for the whole input, so one Z_FINISH
  // call completes the stream.
  const int rv = deflate(&zs, Z_FINISH);
  const size_t produced = zs.total_out;
  deflateEnd(&zs);

  if (rv != Z_STREAM_END) return std::nullopt;

  out.resize(produced);
  return out;
}

} // namespace nprpc::impl
