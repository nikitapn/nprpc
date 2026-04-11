
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
};
