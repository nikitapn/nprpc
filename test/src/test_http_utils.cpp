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
