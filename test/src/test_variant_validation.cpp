// Copyright (c) 2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <cstring>
#include <limits>

// Include the implementation to exercise the private generated request validators
// directly, without needing a transport or nameserver.
#include "nprpc_test.cpp"

using namespace nprpc::test;

namespace {
template <typename> struct RequestType;
template <typename R, typename B, typename T>
struct RequestType<R (*)(B, T&)> { using type = T; };

bool validate_plates(std::initializer_list<std::pair<unsigned, uint32_t>> edits = {})
{
  nprpc::flat_buffer buf;
  auto memory = buf.prepare(64);
  std::memset(memory.data(), 0, 64);
  buf.commit(64);
  auto write = [&](unsigned offset, uint32_t value) {
    std::memcpy(static_cast<char*>(buf.data().data()) + offset, &value, 4);
  };
  // One plate, containing one texture variant and its three-byte string.
  for (auto [offset, value] : {std::pair{0u, 8u}, {4u, 1u}, {24u, 8u},
                              {28u, 1u}, {32u, 2u}, {36u, 8u}, {48u, 8u}, {52u, 3u}})
    write(offset, value);
  for (auto [offset, value] : edits) write(offset, value);
  RequestType<decltype(&check_1VEffectPlate)>::type request(buf, 0);
  return check_1VEffectPlate(buf, request);
}
}

TEST(VariantValidation, AcceptsValidArmsAndEmptyVectors)
{
  EXPECT_TRUE(validate_plates());
  EXPECT_TRUE(validate_plates({{32, 0}}));
  EXPECT_TRUE(validate_plates({{32, 1}}));
  EXPECT_TRUE(validate_plates({{4, 0}}));
  EXPECT_TRUE(validate_plates({{28, 0}}));
}

TEST(VariantValidation, RejectsMalformedVariantVectors)
{
  constexpr auto max = std::numeric_limits<uint32_t>::max();
  for (auto [offset, value] : {std::pair{32u, 3u}, {32u, max},
                              {36u, 0u}, {36u, max}, {36u, 7u}, {36u, 24u},
                              {48u, max}, {52u, max}, {24u, 9u}, {28u, max},
                              {0u, 9u}, {4u, max}, {28u, 0x20000000u},
                              {4u, 0x20000000u}}) {
    SCOPED_TRACE(std::to_string(offset) + ": " + std::to_string(value));
    EXPECT_FALSE(validate_plates({{offset, value}}));
  }
}

TEST(VariantValidation, RejectsInvalidStandaloneVariants)
{
  nprpc::flat_buffer buf;
  auto memory = buf.prepare(8);
  std::memset(memory.data(), 0, 8);
  buf.commit(8);
  RequestType<decltype(&check_1WTestVariant)>::type request(buf, 0);
  EXPECT_FALSE(check_1WTestVariant(buf, request)); // missing payload
  request._1().alloc_arm(sizeof(flat::VariantPayloadA), alignof(flat::VariantPayloadA));
  EXPECT_TRUE(check_1WTestVariant(buf, request));
  request._1().set_kind(std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(check_1WTestVariant(buf, request));
  flat::TestVariant_Direct truncated(buf, static_cast<uint32_t>(buf.size() - 4));
  EXPECT_FALSE(truncated._check_size_align(buf.size()));
  flat::TestVariant_Direct beyond(buf, std::numeric_limits<uint32_t>::max());
  EXPECT_FALSE(beyond._check_size_align(buf.size()));
}
