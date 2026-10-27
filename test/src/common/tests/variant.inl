
class TestVariantRpcImpl : public nprpc::test::ITestVariantRpc_Servant {
public:
  void Echo(nprpc::test::flat::TestVariant_Direct value,
            nprpc::test::flat::TestVariant_Direct result) override
  {
    using Kind = nprpc::test::TestVariant::Kind;
    switch (static_cast<Kind>(value.kind())) {
    case Kind::payloadA: {
      auto src = value.value_payloadA();
      result.alloc_arm(sizeof(nprpc::test::flat::VariantPayloadA),
                       alignof(nprpc::test::flat::VariantPayloadA));
      result.set_kind(static_cast<uint32_t>(Kind::payloadA));
      auto dst = result.value_payloadA();
      dst.id() = src.id();
      dst.label(std::string(src.label()));
      break;
    }
    case Kind::payloadB: {
      auto src = value.value_payloadB();
      result.alloc_arm(sizeof(nprpc::test::flat::VariantPayloadB),
                       alignof(nprpc::test::flat::VariantPayloadB));
      result.set_kind(static_cast<uint32_t>(Kind::payloadB));
      auto dst = result.value_payloadB();
      dst.code() = src.code();
      dst.detail(std::string(src.detail()));
      break;
    }
    }
  }

  // Through the generated helpers, both ways, so a call exercises from_flat
  // and to_flat on a variant nested in a vector inside a message in a vector.
  // Copied out first: the reply is written while arms are allocated, which
  // grows the buffer under any view into it.
  void EchoPlates(
      ::nprpc::flat::Span_ref<nprpc::test::flat::EffectPlate,
                              nprpc::test::flat::EffectPlate_Direct> plates,
      ::nprpc::flat::Vector_Direct2<nprpc::test::flat::EffectPlate,
                                    nprpc::test::flat::EffectPlate_Direct> result) override
  {
    std::vector<nprpc::test::EffectPlate> copy;
    for (auto plate : plates)
      copy.push_back(nprpc::test::helpers::EffectPlate::from_flat(plate));
    result.length(static_cast<std::uint32_t>(copy.size()));
    auto it = copy.begin();
    for (auto plate : result())
      nprpc::test::helpers::EffectPlate::to_flat(plate, *it++);
  }
};
