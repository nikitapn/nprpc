class TestFixedSizeArrayTestImpl : public nprpc::test::IFixedSizeArrayTest_Servant
{
public:
  void InFixedArray (::nprpc::flat::Span<uint32_t> a) override {
    if (a.size() != 10) {
      throw nprpc::test::AssertionFailed{"Expected array of size 10"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i] != i + 1) {
        throw nprpc::test::AssertionFailed{"Unexpected value in input array"};
      }
    }
  }

  void OutFixedArray (::nprpc::flat::Span<uint32_t> a) override {
    if (a.size() != 10) {
      throw nprpc::test::AssertionFailed{"Expected array of size 10"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      a[i] = static_cast<uint32_t>(i + 1);
    }
  }

  void OutTwoFixedArrays (::nprpc::flat::Span<uint32_t> a, ::nprpc::flat::Span<uint32_t> b) override {
    if (a.size() != 10 || b.size() != 10) {
      throw nprpc::test::AssertionFailed{"Expected arrays of size 10"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      a[i] = static_cast<uint32_t>(i + 1);
      b[i] = static_cast<uint32_t>((i + 1) * 10);
    }
  }

  void InFixedArrayOfStructs (::nprpc::flat::Span_ref<::nprpc::test::flat::SimpleStruct, ::nprpc::test::flat::SimpleStruct_Direct> a) override {
    if (a.size() != 5) {
      throw nprpc::test::AssertionFailed{"Expected array of size 5"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      if ((*a[i]).id() != i + 1) {
        throw nprpc::test::AssertionFailed{"Unexpected value in input array"};
      }
    }
  }

  void OutFixedArrayOfStructs (::nprpc::flat::Span_ref<::nprpc::test::flat::SimpleStruct, ::nprpc::test::flat::SimpleStruct_Direct> a) override {
    if (a.size() != 5) {
      throw nprpc::test::AssertionFailed{"Expected array of size 5"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      (*a[i]).id() = static_cast<uint32_t>(i + 1);
    }
  }

  void OutTwoFixedArraysOfStructs (
    ::nprpc::flat::Span_ref<::nprpc::test::flat::SimpleStruct, ::nprpc::test::flat::SimpleStruct_Direct> a,
    ::nprpc::flat::Span_ref<::nprpc::test::flat::AAA, ::nprpc::test::flat::AAA_Direct> b) override
  {
    if (a.size() != 5 || b.size() != 5) {
      throw nprpc::test::AssertionFailed{"Expected arrays of size 5"};
    }
    for (size_t i = 0; i < a.size(); ++i) {
      (*a[i]).id() = static_cast<uint32_t>(i + 1);
      (*b[i]).a() = static_cast<uint32_t>((i + 1) * 10);
      (*b[i]).b("str" + std::to_string(i + 1));
      (*b[i]).c("str" + std::to_string((i + 1) * 100));
    }
  }
};

