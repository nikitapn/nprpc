// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <nprpc/impl/http_utils.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

class TempDir {
public:
  TempDir()
  {
    const auto base = std::filesystem::temp_directory_path();
    for (uint32_t attempt = 0; attempt != 128; ++attempt) {
      const auto candidate =
          base /
          ("nprpc-http-utils-" +
           std::to_string(std::chrono::steady_clock::now()
                              .time_since_epoch()
                              .count()) +
           "-" + std::to_string(attempt));

      std::error_code ec;
      if (std::filesystem::create_directories(candidate, ec)) {
        path_ = candidate;
        return;
      }
    }

    throw std::runtime_error("failed to create temporary directory");
  }

  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

} // namespace

TEST(HttpUtils, ResolvesPathInsideDocRoot)
{
  TempDir doc_root;
  std::filesystem::create_directories(doc_root.path() / "assets");

  const auto resolved = nprpc::impl::resolve_http_doc_root_path(
      doc_root.path().string(), "/assets/app.js");

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved,
            std::filesystem::weakly_canonical(doc_root.path() / "assets/app.js"));
}

TEST(HttpUtils, RejectsParentTraversal)
{
  TempDir doc_root;

  const auto resolved = nprpc::impl::resolve_http_doc_root_path(
      doc_root.path().string(), "/../../etc/passwd");

  EXPECT_FALSE(resolved.has_value());
}

TEST(HttpUtils, RejectsTraversalAfterNormalization)
{
  TempDir doc_root;

  const auto resolved = nprpc::impl::resolve_http_doc_root_path(
      doc_root.path().string(), "/static/../..//secret.txt");

  EXPECT_FALSE(resolved.has_value());
}

TEST(HttpUtils, ParsesValidContentLength)
{
  const auto parsed = nprpc::impl::parse_http_content_length("12345");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(*parsed, 12345u);
}

TEST(HttpUtils, RejectsEmptyContentLength)
{
  EXPECT_FALSE(nprpc::impl::parse_http_content_length("").has_value());
}

TEST(HttpUtils, RejectsNonNumericContentLength)
{
  EXPECT_FALSE(nprpc::impl::parse_http_content_length("abc").has_value());
  EXPECT_FALSE(nprpc::impl::parse_http_content_length("12x").has_value());
}

TEST(HttpUtils, RejectsOutOfRangeContentLength)
{
  EXPECT_FALSE(nprpc::impl::parse_http_content_length(
      "184467440737095516160").has_value());
}

#ifndef _WIN32
TEST(HttpUtils, RejectsSymlinkEscape)
{
  TempDir sandbox;
  const auto doc_root = sandbox.path() / "www";
  const auto outside = sandbox.path() / "outside";

  std::filesystem::create_directories(doc_root);
  std::filesystem::create_directories(outside);
  std::ofstream(outside / "secret.txt") << "secret";
  std::filesystem::create_directory_symlink(outside, doc_root / "public-link");

  const auto resolved = nprpc::impl::resolve_http_doc_root_path(
      doc_root.string(), "/public-link/secret.txt");

  EXPECT_FALSE(resolved.has_value());
}
#endif

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
TEST(HttpUtils, ParsesImfFixdate)
{
  using namespace std::chrono;
  auto const t = nprpc::impl::parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT");
  ASSERT_TRUE(t.has_value());
  auto const sys = clock_cast<system_clock>(*t);
  EXPECT_EQ(floor<seconds>(sys).time_since_epoch().count(), 784111777);
}

TEST(HttpUtils, ParsesLeapDay)
{
  using namespace std::chrono;
  auto const t = nprpc::impl::parse_http_date("Thu, 29 Feb 2024 00:00:00 GMT");
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(floor<seconds>(clock_cast<system_clock>(*t)).time_since_epoch().count(),
            1709164800);
}

TEST(HttpUtils, RejectsOtherDateFormats)
{
  using nprpc::impl::parse_http_date;
  EXPECT_FALSE(parse_http_date(""));
  EXPECT_FALSE(parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT"));  // RFC 850
  EXPECT_FALSE(parse_http_date("Sun Nov  6 08:49:37 1994"));        // asctime
  EXPECT_FALSE(parse_http_date("Sun, 06 Foo 1994 08:49:37 GMT"));
  EXPECT_FALSE(parse_http_date("Sun, 06 Nov 1994 25:49:37 GMT"));
  EXPECT_FALSE(parse_http_date("Sun, 06 Nov 1994 08:49:37 UTC"));
}

TEST(HttpUtils, NotModifiedSinceComparesWholeSeconds)
{
  using namespace std::chrono;
  using nprpc::impl::not_modified_since;
  // Last-Modified drops the sub-second part; a client echoing it back must
  // still get a 304.
  auto const mtime = clock_cast<std::filesystem::file_time_type::clock>(
      sys_seconds{seconds{784111777}} + milliseconds{750});
  EXPECT_TRUE(not_modified_since(mtime, "Sun, 06 Nov 1994 08:49:37 GMT"));
  EXPECT_TRUE(not_modified_since(mtime, "Sun, 06 Nov 1994 08:49:38 GMT"));
  EXPECT_FALSE(not_modified_since(mtime, "Sun, 06 Nov 1994 08:49:36 GMT"));
  EXPECT_FALSE(not_modified_since(mtime, "garbage"));
}

TEST(HttpUtils, IfNoneMatchComparesWeakly)
{
  using nprpc::impl::if_none_match_matches;
  constexpr std::string_view etag = "\"1a2b-gzip\"";
  EXPECT_TRUE(if_none_match_matches("\"1a2b-gzip\"", etag));
  EXPECT_TRUE(if_none_match_matches("W/\"1a2b-gzip\"", etag));
  EXPECT_TRUE(if_none_match_matches("*", etag));
  EXPECT_TRUE(if_none_match_matches("\"x\", \"1a2b-gzip\"", etag));
  EXPECT_TRUE(if_none_match_matches("\"x\",W/\"1a2b-gzip\" ", etag));
  EXPECT_TRUE(if_none_match_matches("\"1a2b\"", "W/\"1a2b\""));
  EXPECT_FALSE(if_none_match_matches("\"1a2b\"", etag));
  EXPECT_FALSE(if_none_match_matches("\"x\", \"y\"", etag));
  EXPECT_FALSE(if_none_match_matches("", etag));
  EXPECT_FALSE(if_none_match_matches(" , ", etag));
}

TEST(HttpUtils, CacheControlPatternWithoutSlashMatchesLastSegment)
{
  using nprpc::impl::cache_control_pattern_matches;
  EXPECT_TRUE(cache_control_pattern_matches("*.html", "/index.html"));
  EXPECT_TRUE(cache_control_pattern_matches("*.html", "/blog/post/a.html"));
  EXPECT_FALSE(cache_control_pattern_matches("*.html", "/a.html/x.js"));
  EXPECT_FALSE(cache_control_pattern_matches("*.html", "/index.htm"));
  EXPECT_TRUE(cache_control_pattern_matches("favicon.ico", "/favicon.ico"));
  EXPECT_TRUE(cache_control_pattern_matches("app.????????.js", "/js/app.1a2b3c4d.js"));
  EXPECT_FALSE(cache_control_pattern_matches("app.????????.js", "/js/app.1a2b3c.js"));
  EXPECT_FALSE(cache_control_pattern_matches("*.HTML", "/index.html"));
  EXPECT_FALSE(cache_control_pattern_matches("", "/index.html"));
}

TEST(HttpUtils, CacheControlPatternWithSlashMatchesWholePath)
{
  using nprpc::impl::cache_control_pattern_matches;
  EXPECT_TRUE(cache_control_pattern_matches("/assets/**", "/assets/app.js"));
  EXPECT_TRUE(cache_control_pattern_matches("/assets/**", "/assets/img/logo.svg"));
  EXPECT_FALSE(cache_control_pattern_matches("/assets/**", "/index.html"));
  EXPECT_FALSE(cache_control_pattern_matches("/assets/**", "/static/assets/a.js"));
  EXPECT_TRUE(cache_control_pattern_matches("/assets/*", "/assets/app.js"));
  EXPECT_FALSE(cache_control_pattern_matches("/assets/*", "/assets/img/logo.svg"));
  EXPECT_TRUE(cache_control_pattern_matches("/**/*.css", "/a/b/c.css"));
  EXPECT_TRUE(cache_control_pattern_matches("/**/*.css", "/c.css"));
  EXPECT_TRUE(cache_control_pattern_matches("/a/**/b", "/a/b"));
  EXPECT_TRUE(cache_control_pattern_matches("/a/**/b", "/a/x/y/b"));
  EXPECT_TRUE(cache_control_pattern_matches("**", "/anything/at/all"));
  EXPECT_FALSE(cache_control_pattern_matches("/?", "/"));
}

TEST(HttpUtils, SelectsFirstMatchingCacheControlRule)
{
  using nprpc::impl::k_static_file_cache_control;
  using nprpc::impl::select_static_cache_control;
  const nprpc::impl::CacheControlRules rules{
      {"/assets/**", "public, max-age=31536000, immutable"},
      {"*.html", "no-cache"},
      {"*.js", "public, max-age=60"},
      {"robots.txt", ""},
  };
  EXPECT_EQ(select_static_cache_control(rules, "/assets/app.js"),
            "public, max-age=31536000, immutable");
  EXPECT_EQ(select_static_cache_control(rules, "/assets/page.html"),
            "public, max-age=31536000, immutable");
  EXPECT_EQ(select_static_cache_control(rules, "/index.html"), "no-cache");
  EXPECT_EQ(select_static_cache_control(rules, "/index.html?v=2"), "no-cache");
  EXPECT_EQ(select_static_cache_control(rules, "/lib.js#x"), "public, max-age=60");
  EXPECT_EQ(select_static_cache_control(rules, "/robots.txt"), "");
  EXPECT_EQ(select_static_cache_control(rules, "/logo.png"),
            k_static_file_cache_control);
  EXPECT_EQ(select_static_cache_control({}, "/index.html"),
            k_static_file_cache_control);
}

namespace {
using Kind = nprpc::impl::ByteRangeSelection::Kind;

void expect_range(std::string_view header, uint64_t size, Kind kind,
                  uint64_t offset = 0, uint64_t length = 0)
{
  SCOPED_TRACE(std::string(header));
  const auto r = nprpc::impl::select_byte_range(header, size);
  EXPECT_EQ(r.kind, kind);
  if (kind == Kind::Partial) {
    EXPECT_EQ(r.offset, offset);
    EXPECT_EQ(r.length, length);
  }
}
} // namespace

TEST(HttpUtils, SelectsSingleByteRange)
{
  expect_range("bytes=0-499", 1000, Kind::Partial, 0, 500);
  expect_range("bytes=500-999", 1000, Kind::Partial, 500, 500);
  expect_range("bytes=500-", 1000, Kind::Partial, 500, 500);
  expect_range("bytes=-200", 1000, Kind::Partial, 800, 200);
  expect_range("bytes=0-0", 1000, Kind::Partial, 0, 1);
  expect_range("bytes=999-", 1000, Kind::Partial, 999, 1);
  // last-pos past the end and suffixes longer than the file are clamped.
  expect_range("bytes=900-5000", 1000, Kind::Partial, 900, 100);
  expect_range("bytes=-5000", 1000, Kind::Partial, 0, 1000);
  // Unit is case-insensitive; OWS and empty list elements are allowed.
  expect_range("Bytes=1-2", 1000, Kind::Partial, 1, 2);
  expect_range(" bytes = 1-2 ", 1000, Kind::Partial, 1, 2);
  expect_range("bytes=, 1-2 ,", 1000, Kind::Partial, 1, 2);
}

TEST(HttpUtils, UnsatisfiableByteRanges)
{
  expect_range("bytes=1000-", 1000, Kind::Unsatisfiable);
  expect_range("bytes=1000-1500", 1000, Kind::Unsatisfiable);
  expect_range("bytes=-0", 1000, Kind::Unsatisfiable);
  expect_range("bytes=0-", 0, Kind::Unsatisfiable);
  expect_range("bytes=-10", 0, Kind::Unsatisfiable);
}

TEST(HttpUtils, IgnoresRangesItDoesNotServe)
{
  expect_range("", 1000, Kind::Full);
  expect_range("items=0-1", 1000, Kind::Full);
  expect_range("bytes", 1000, Kind::Full);
  expect_range("bytes=", 1000, Kind::Full);
  expect_range("bytes=0-1,5-6", 1000, Kind::Full);
  expect_range("bytes=5-1", 1000, Kind::Full);
  expect_range("bytes=a-1", 1000, Kind::Full);
  expect_range("bytes=1", 1000, Kind::Full);
  expect_range("bytes=-", 1000, Kind::Full);
  expect_range("bytes=99999999999999999999-", 1000, Kind::Full);
}

TEST(HttpUtils, IfRangeComparesStronglyOrByExactDate)
{
  using namespace std::chrono;
  using nprpc::impl::if_range_matches;
  auto const mtime = clock_cast<std::filesystem::file_time_type::clock>(
      sys_seconds{seconds{784111777}} + milliseconds{750});
  EXPECT_TRUE(if_range_matches("\"1a2b\"", "\"1a2b\"", mtime));
  EXPECT_TRUE(if_range_matches(" \"1a2b\" ", "\"1a2b\"", mtime));
  EXPECT_FALSE(if_range_matches("W/\"1a2b\"", "\"1a2b\"", mtime));
  EXPECT_FALSE(if_range_matches("\"1a2c\"", "\"1a2b\"", mtime));
  EXPECT_TRUE(if_range_matches("Sun, 06 Nov 1994 08:49:37 GMT", "\"x\"", mtime));
  // Unlike If-Modified-Since, a later date is not a match.
  EXPECT_FALSE(if_range_matches("Sun, 06 Nov 1994 08:49:38 GMT", "\"x\"", mtime));
  EXPECT_FALSE(if_range_matches("garbage", "\"x\"", mtime));
  EXPECT_FALSE(if_range_matches("", "\"x\"", mtime));
}
