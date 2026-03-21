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