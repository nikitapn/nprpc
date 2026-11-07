// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <nprpc/impl/http_compression.hpp>

#include <brotli/decode.h>
#include <zlib.h>

#include <string>
#include <vector>

using nprpc::impl::ContentEncoding;
using nprpc::impl::compress_body;
using nprpc::impl::negotiate_content_encoding;
using nprpc::impl::static_compression_level;

namespace {

std::string sample_text()
{
  std::string s;
  for (int i = 0; i < 400; ++i)
    s += "<li class=\"post\">Hello, NPRPC " + std::to_string(i % 17) + "</li>\n";
  return s;
}

std::string inflate_zlib(const std::vector<uint8_t>& in, int window_bits)
{
  z_stream zs{};
  EXPECT_EQ(inflateInit2(&zs, window_bits), Z_OK);
  std::string out(1 << 20, '\0');
  zs.next_in = const_cast<Bytef*>(in.data());
  zs.avail_in = static_cast<uInt>(in.size());
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  EXPECT_EQ(inflate(&zs, Z_FINISH), Z_STREAM_END);
  out.resize(zs.total_out);
  inflateEnd(&zs);
  return out;
}

std::string decode_brotli(const std::vector<uint8_t>& in)
{
  std::string out(1 << 20, '\0');
  size_t out_len = out.size();
  EXPECT_EQ(BrotliDecoderDecompress(in.size(), in.data(), &out_len,
                                    reinterpret_cast<uint8_t*>(out.data())),
            BROTLI_DECODER_RESULT_SUCCESS);
  out.resize(out_len);
  return out;
}

} // namespace

TEST(HttpCompression, NegotiatePrefersBrotliOnTie)
{
  EXPECT_EQ(negotiate_content_encoding("gzip, deflate, br"), ContentEncoding::Brotli);
  EXPECT_EQ(negotiate_content_encoding("gzip, deflate, br, zstd"), ContentEncoding::Brotli);
  EXPECT_EQ(negotiate_content_encoding("*"), ContentEncoding::Brotli);
  EXPECT_EQ(negotiate_content_encoding("BR"), ContentEncoding::Brotli);
}

TEST(HttpCompression, NegotiateHonoursQValues)
{
  EXPECT_EQ(negotiate_content_encoding("br;q=0.5, gzip"), ContentEncoding::Gzip);
  EXPECT_EQ(negotiate_content_encoding("br;q=0, gzip;q=0.1"), ContentEncoding::Gzip);
  EXPECT_EQ(negotiate_content_encoding("br;q=0, *"), ContentEncoding::Gzip);
  EXPECT_EQ(negotiate_content_encoding("br;q=0, gzip;q=0, *;q=0.3"),
            ContentEncoding::Deflate);
  EXPECT_EQ(negotiate_content_encoding("br;q=0, gzip;q=0, deflate;q=0"),
            ContentEncoding::Identity);
  EXPECT_EQ(negotiate_content_encoding("gzip;q=0.8, br;q=0.9"), ContentEncoding::Brotli);
}

TEST(HttpCompression, NegotiateWithoutBrotli)
{
  // Clients that never mention br keep getting what they got before.
  EXPECT_EQ(negotiate_content_encoding("gzip, deflate"), ContentEncoding::Gzip);
  EXPECT_EQ(negotiate_content_encoding("deflate"), ContentEncoding::Deflate);
  EXPECT_EQ(negotiate_content_encoding(""), ContentEncoding::Identity);
  EXPECT_EQ(negotiate_content_encoding("identity"), ContentEncoding::Identity);
}

TEST(HttpCompression, TokenForBrotli)
{
  EXPECT_EQ(nprpc::impl::content_encoding_token(ContentEncoding::Brotli), "br");
}

TEST(HttpCompression, RoundTripsEveryCoding)
{
  const auto text = sample_text();
  const auto* data = reinterpret_cast<const uint8_t*>(text.data());

  auto gz = compress_body(data, text.size(), ContentEncoding::Gzip);
  ASSERT_TRUE(gz);
  EXPECT_LT(gz->size(), text.size());
  EXPECT_EQ(inflate_zlib(*gz, 15 + 16), text);

  auto zl = compress_body(data, text.size(), ContentEncoding::Deflate);
  ASSERT_TRUE(zl);
  EXPECT_EQ(inflate_zlib(*zl, 15), text);

  auto br = compress_body(data, text.size(), ContentEncoding::Brotli);
  ASSERT_TRUE(br);
  EXPECT_LT(br->size(), gz->size());
  EXPECT_EQ(decode_brotli(*br), text);

  auto br11 = compress_body(data, text.size(), ContentEncoding::Brotli,
                            static_compression_level(ContentEncoding::Brotli,
                                                     text.size()));
  ASSERT_TRUE(br11);
  EXPECT_LE(br11->size(), br->size());
  EXPECT_EQ(decode_brotli(*br11), text);

  EXPECT_FALSE(compress_body(data, text.size(), ContentEncoding::Identity));
}

TEST(HttpCompression, BrotliHandlesEmptyInput)
{
  auto br = compress_body(nullptr, 0, ContentEncoding::Brotli);
  ASSERT_TRUE(br);
  EXPECT_EQ(decode_brotli(*br), "");
}

TEST(HttpCompression, StaticLevelDropsForLargeFiles)
{
  EXPECT_EQ(static_compression_level(ContentEncoding::Brotli, 10 * 1024), 11);
  EXPECT_LT(static_compression_level(ContentEncoding::Brotli, 4 * 1024 * 1024), 11);
  EXPECT_EQ(static_compression_level(ContentEncoding::Gzip, 10 * 1024), -1);
}
