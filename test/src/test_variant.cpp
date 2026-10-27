// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Unit tests for the IDL-generated "one of" (variant) feature.
// These tests exercise the generated C++ types only — no RPC or nameserver needed.

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <variant>

#include <nprpc/flat_buffer.hpp>
#include <nprpc_test.hpp>

using namespace nprpc::test;

// Helper: create a flat_buffer pre-committed to hold a T at offset 0.
template<typename T>
static nprpc::flat_buffer make_flat_buf()
{
  nprpc::flat_buffer buf;
  auto mb = buf.prepare(sizeof(T));
  std::memset(mb.data(), 0, sizeof(T));
  buf.commit(sizeof(T));
  return buf;
}

// ─── Kind enum ───────────────────────────────────────────────────────────────

TEST(VariantTest, KindEnumValues)
{
  EXPECT_EQ(static_cast<uint32_t>(TestVariant::Kind::payloadA), 0u);
  EXPECT_EQ(static_cast<uint32_t>(TestVariant::Kind::payloadB), 1u);
}

// ─── Construction ────────────────────────────────────────────────────────────

TEST(VariantTest, ConstructWithPayloadA)
{
  // Aggregate-initialise: kind then value (variant converts from VariantPayloadA).
  TestVariant v{TestVariant::Kind::payloadA, VariantPayloadA{42u, "hello"}};

  EXPECT_EQ(v.kind, TestVariant::Kind::payloadA);
  ASSERT_TRUE(std::holds_alternative<VariantPayloadA>(v.value));

  auto& a = std::get<VariantPayloadA>(v.value);
  EXPECT_EQ(a.id, 42u);
  EXPECT_EQ(a.label, "hello");
}

TEST(VariantTest, ConstructWithPayloadB)
{
  TestVariant v{TestVariant::Kind::payloadB, VariantPayloadB{99u, "detail text"}};

  EXPECT_EQ(v.kind, TestVariant::Kind::payloadB);
  ASSERT_TRUE(std::holds_alternative<VariantPayloadB>(v.value));

  auto& b = std::get<VariantPayloadB>(v.value);
  EXPECT_EQ(b.code, 99u);
  EXPECT_EQ(b.detail, "detail text");
}

// ─── std::visit dispatch ─────────────────────────────────────────────────────

TEST(VariantTest, StdVisitDispatch)
{
  auto as_string = [](const TestVariant& v) -> std::string {
    return std::visit(
        [](auto&& arg) -> std::string {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, VariantPayloadA>)
            return "A:" + arg.label;
          else if constexpr (std::is_same_v<T, VariantPayloadB>)
            return "B:" + std::to_string(arg.code);
          else
            return "unknown";
        },
        v.value);
  };

  TestVariant va{TestVariant::Kind::payloadA, VariantPayloadA{0u, "dispatch"}};
  EXPECT_EQ(as_string(va), "A:dispatch");

  TestVariant vb{TestVariant::Kind::payloadB, VariantPayloadB{7u, ""}};
  EXPECT_EQ(as_string(vb), "B:7");
}

// ─── Mutation ────────────────────────────────────────────────────────────────

TEST(VariantTest, ReassignToOtherArm)
{
  TestVariant v{TestVariant::Kind::payloadA, VariantPayloadA{1u, "initial"}};

  // Re-assign to the other arm.
  v.kind  = TestVariant::Kind::payloadB;
  v.value = VariantPayloadB{55u, "reassigned"};

  EXPECT_EQ(v.kind, TestVariant::Kind::payloadB);
  ASSERT_TRUE(std::holds_alternative<VariantPayloadB>(v.value));
  EXPECT_EQ(std::get<VariantPayloadB>(v.value).code, 55u);
}

// ─── Move semantics ───────────────────────────────────────────────────────────

TEST(VariantTest, MovePreservesValue)
{
  const std::string long_label(128, 'x'); // forces heap alloc
  TestVariant v1{TestVariant::Kind::payloadA, VariantPayloadA{3u, long_label}};

  TestVariant v2 = std::move(v1);

  EXPECT_EQ(v2.kind, TestVariant::Kind::payloadA);
  EXPECT_EQ(std::get<VariantPayloadA>(v2.value).label, long_label);
}

// ─── value_type typedef ──────────────────────────────────────────────────────

TEST(VariantTest, ValueTypeIsVariant)
{
  // The generated alias `value_type` must be std::variant<VariantPayloadA, VariantPayloadB>.
  static_assert(
      std::is_same_v<
          TestVariant::value_type,
          std::variant<VariantPayloadA, VariantPayloadB>>,
      "TestVariant::value_type should be std::variant<VariantPayloadA, VariantPayloadB>");
}

// ─── VariantContainer ────────────────────────────────────────────────────────

TEST(VariantTest, ContainerEmbeddedVariant)
{
  VariantContainer c;
  c.id           = 10u;
  c.payload.kind = TestVariant::Kind::payloadA;
  c.payload.value = VariantPayloadA{77u, "embedded"};

  EXPECT_EQ(c.id, 10u);
  EXPECT_EQ(c.payload.kind, TestVariant::Kind::payloadA);
  EXPECT_EQ(std::get<VariantPayloadA>(c.payload.value).id, 77u);
}

// ─── Serialization round-trips ───────────────────────────────────────────────

// Serialize a VariantContainer with arm payloadA into flat, then from_flat it.
TEST(VariantTest, SerializeRoundTrip_PayloadA)
{
  VariantContainer src;
  src.id           = 42u;
  src.payload.kind  = TestVariant::Kind::payloadA;
  src.payload.value = VariantPayloadA{7u, "hello"};

  auto buf = make_flat_buf<flat::VariantContainer>();
  flat::VariantContainer_Direct dest(buf, 0);
  helpers::VariantContainer::to_flat(dest, src);

  auto result = helpers::VariantContainer::from_flat(dest);

  EXPECT_EQ(result.id, 42u);
  EXPECT_EQ(result.payload.kind, TestVariant::Kind::payloadA);
  ASSERT_TRUE(std::holds_alternative<VariantPayloadA>(result.payload.value));
  auto& a = std::get<VariantPayloadA>(result.payload.value);
  EXPECT_EQ(a.id, 7u);
  EXPECT_EQ(a.label, "hello");
}

// Same but arm payloadB.
TEST(VariantTest, SerializeRoundTrip_PayloadB)
{
  VariantContainer src;
  src.id           = 99u;
  src.payload.kind  = TestVariant::Kind::payloadB;
  src.payload.value = VariantPayloadB{55u, "world"};

  auto buf = make_flat_buf<flat::VariantContainer>();
  flat::VariantContainer_Direct dest(buf, 0);
  helpers::VariantContainer::to_flat(dest, src);

  auto result = helpers::VariantContainer::from_flat(dest);

  EXPECT_EQ(result.id, 99u);
  EXPECT_EQ(result.payload.kind, TestVariant::Kind::payloadB);
  ASSERT_TRUE(std::holds_alternative<VariantPayloadB>(result.payload.value));
  auto& b = std::get<VariantPayloadB>(result.payload.value);
  EXPECT_EQ(b.code, 55u);
  EXPECT_EQ(b.detail, "world");
}

// Serialize payloadA, then serialize payloadB into a fresh buffer — no cross-contamination.
TEST(VariantTest, SerializeTwoDifferentArms)
{
  VariantContainer src_a;
  src_a.id           = 1u;
  src_a.payload.kind  = TestVariant::Kind::payloadA;
  src_a.payload.value = VariantPayloadA{10u, "arm-a"};

  VariantContainer src_b;
  src_b.id           = 2u;
  src_b.payload.kind  = TestVariant::Kind::payloadB;
  src_b.payload.value = VariantPayloadB{20u, "arm-b"};

  auto buf_a = make_flat_buf<flat::VariantContainer>();
  flat::VariantContainer_Direct da(buf_a, 0);
  helpers::VariantContainer::to_flat(da, src_a);

  auto buf_b = make_flat_buf<flat::VariantContainer>();
  flat::VariantContainer_Direct db(buf_b, 0);
  helpers::VariantContainer::to_flat(db, src_b);

  auto result_a = helpers::VariantContainer::from_flat(da);
  auto result_b = helpers::VariantContainer::from_flat(db);

  EXPECT_EQ(result_a.id, 1u);
  EXPECT_EQ(std::get<VariantPayloadA>(result_a.payload.value).label, "arm-a");

  EXPECT_EQ(result_b.id, 2u);
  EXPECT_EQ(std::get<VariantPayloadB>(result_b.payload.value).detail, "arm-b");
}

// Empty string arm label — tests zero-length string edge case.
TEST(VariantTest, SerializeRoundTrip_EmptyString)
{
  VariantContainer src;
  src.id           = 0u;
  src.payload.kind  = TestVariant::Kind::payloadA;
  src.payload.value = VariantPayloadA{0u, ""};

  auto buf = make_flat_buf<flat::VariantContainer>();
  flat::VariantContainer_Direct dest(buf, 0);
  helpers::VariantContainer::to_flat(dest, src);

  auto result = helpers::VariantContainer::from_flat(dest);
  EXPECT_EQ(std::get<VariantPayloadA>(result.payload.value).label, "");
}

// ─── A variant nested in a vector inside a message ───────────────────────────
//
// `EffectPlate.effects` is `vector<PlateEffect>`: each element is the
// variant's 8-byte slot, its arm allocated out of line. The shape a list of
// plates with stacking effects takes.

static void expect_same(const EffectPlate& a, const EffectPlate& b)
{
  EXPECT_EQ(a.x, b.x);
  EXPECT_EQ(a.y, b.y);
  EXPECT_EQ(a.w, b.w);
  EXPECT_EQ(a.h, b.h);
  ASSERT_EQ(a.effects.size(), b.effects.size());
  for (size_t i = 0; i < a.effects.size(); ++i) {
    SCOPED_TRACE("effect " + std::to_string(i));
    const auto& ea = a.effects[i];
    const auto& eb = b.effects[i];
    ASSERT_EQ(ea.kind, eb.kind);
    switch (ea.kind) {
    case PlateEffect::Kind::blur:
      EXPECT_EQ(std::get<EffectBlur>(ea.value).radius,
                std::get<EffectBlur>(eb.value).radius);
      break;
    case PlateEffect::Kind::glass: {
      const auto& ga = std::get<EffectGlass>(ea.value);
      const auto& gb = std::get<EffectGlass>(eb.value);
      EXPECT_EQ(ga.refraction, gb.refraction);
      EXPECT_EQ(ga.rim, gb.rim);
      EXPECT_EQ(ga.magnification, gb.magnification);
      break;
    }
    case PlateEffect::Kind::texture: {
      const auto& ta = std::get<EffectTexture>(ea.value);
      const auto& tb = std::get<EffectTexture>(eb.value);
      EXPECT_EQ(ta.strength, tb.strength);
      EXPECT_EQ(ta.name, tb.name);
      break;
    }
    }
  }
}

// The value the cross-language fixtures below encode. Keep in step with
// `samplePlate()` in nprpc_swift's MarshallingHelpersTests.
static EffectPlate sample_plate()
{
  EffectPlate p;
  p.x = 1.5f;
  p.y = 2.0f;
  p.w = 300.0f;
  p.h = 40.0f;
  p.effects = {
    {PlateEffect::Kind::blur, EffectBlur{12.0f}},
    {PlateEffect::Kind::glass, EffectGlass{10.0f, 0.8f, 1.35f}},
    {PlateEffect::Kind::texture, EffectTexture{0.5f, "grain"}},
    {PlateEffect::Kind::texture, EffectTexture{0.0f, ""}},
  };
  return p;
}

static EffectPlate round_trip(const EffectPlate& src)
{
  auto buf = make_flat_buf<flat::EffectPlate>();
  flat::EffectPlate_Direct dest(buf, 0);
  helpers::EffectPlate::to_flat(dest, src);
  return helpers::EffectPlate::from_flat(dest);
}

TEST(VariantTest, NestedVectorOfVariants)
{
  const auto src = sample_plate();
  expect_same(round_trip(src), src);
}

TEST(VariantTest, NestedVectorOfVariants_Empty)
{
  EffectPlate src{};
  src.w = 10.0f;
  const auto out = round_trip(src);
  EXPECT_EQ(out.w, 10.0f);
  EXPECT_TRUE(out.effects.empty());
}

// Arms allocated while the element slots are being walked grow the buffer
// under that walk — enough of them, with strings, to force several
// reallocations.
TEST(VariantTest, NestedVectorOfVariants_GrowsBuffer)
{
  EffectPlate src{};
  for (int i = 0; i < 40; ++i) {
    switch (i % 3) {
    case 0:
      src.effects.push_back({PlateEffect::Kind::blur, EffectBlur{float(i)}});
      break;
    case 1:
      src.effects.push_back(
          {PlateEffect::Kind::glass, EffectGlass{float(i), 0.5f, 1.0f + i}});
      break;
    default:
      src.effects.push_back({PlateEffect::Kind::texture,
                             EffectTexture{float(i), std::string(200 + i, 'a' + i % 26)}});
      break;
    }
  }
  expect_same(round_trip(src), src);
}
// ─── Across languages ────────────────────────────────────────────────────────
//
// What nprpc_swift's generated marshal writes for `sample_plate()`. It is not
// the same bytes as this side's — Swift allocates every variant arm a fixed
// 128 bytes (`marshal_optional_struct`), C++ exactly sizeof — so equality of
// encodings proves nothing; what has to hold is that each side reads the
// other's. The mirror of this test is in MarshallingHelpersTests.swift.
//
// Regenerate from `MarshallingHelpersTests.encode(samplePlate())` if the
// encoding changes on purpose.
static const char kSwiftSamplePlateHex[] =
  "0000c03f0000004000009643000020420800000004000000000000001c000000"
  "0100000094000000020000000c010000020000008c0100000000404100000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "00000000000000000000000000000000000000000000000000002041cdcc4c3f"
  "cdccac3f00000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000003f7c000000"
  "0500000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "000000000000000000000000000000000000000000000000677261696e000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000"
  "3c00000000000000000000000000000000000000000000000000000000000000"
  "0000000000000000000000000000000000000000000000000000000000000000";

TEST(VariantTest, NestedVectorOfVariants_DecodesSwiftEncoding)
{
  const std::string hex = kSwiftSamplePlateHex;
  ASSERT_EQ(hex.size() % 2, 0u);
  nprpc::flat_buffer buf;
  auto mb = buf.prepare(hex.size() / 2);
  auto* out = static_cast<unsigned char*>(mb.data());
  for (size_t i = 0; i < hex.size() / 2; ++i)
    out[i] = static_cast<unsigned char>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
  buf.commit(hex.size() / 2);

  flat::EffectPlate_Direct src(buf, 0);
  expect_same(helpers::EffectPlate::from_flat(src), sample_plate());
}
