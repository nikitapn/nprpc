
class TestDirectImpl : public nprpc::test::ITestDirect_Servant {
public:
  // Fill result with a simple counter sequence [0, 1, 2, ..., size-1]
  void GetBytes(uint32_t size, /*out*/ ::nprpc::flat::Vector_Direct1<uint8_t> result) override {
    result.length(size);
    auto span = result();
    for (uint32_t i = 0; i < size; ++i)
      span[i] = static_cast<uint8_t>(i % 256);
  }

  // Fill result with a simple counter sequence [0, 1, 2, ..., size-1]
  void GetBytesFixedArray (/*out*/ ::nprpc::flat::Span<uint8_t> result) override {
    for (uint32_t i = 0; i < result.size(); ++i)
      result[i] = static_cast<uint8_t>(i % 256);
  }

  // Fill result with well-known values so the client can assert them
  void GetFlatStruct(nprpc::test::flat::FlatStruct_Direct result) override {
    result.a() = 42;
    result.b() = 100u;
    result.c() = 3.14f;
  }

  // Return a fixed string
  void GetString(::nprpc::flat::String_Direct1 result) override {
    result = "Hello, direct!";
  }

  // Fill result with count structs whose id equals their 1-based index
  void GetStructArray(uint32_t count, /*out*/ ::nprpc::flat::Vector_Direct2<
      nprpc::test::flat::SimpleStruct,
      nprpc::test::flat::SimpleStruct_Direct> result) override {
    result.length(count);
    auto span = result();
    uint32_t i = 1;
    for (auto s : span)
      s.id() = i++;
  }

  // 'direct' on a fundamental is demoted to plain out — regular reference
  void GetFundamentalDirect(uint32_t& result) override {
    result = 777;
  }
};
