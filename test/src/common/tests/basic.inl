
class TestBasicImpl : public nprpc::test::ITestBasic_Servant {
public:
    uint32_t ReturnU32() override{
        return 42;
    }

    bool ReturnBoolean() override{
        return true;
    }

    bool In_(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) override {
        EXPECT_EQ(a, 100u);
        EXPECT_TRUE(b.get());

        uint8_t ix = 0;
        for (auto i : c) {
            EXPECT_EQ(ix++, i);
        }

        return true;
    }

    void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) override {
        a = 100;
        b = true;

        c.length(256);
        auto span = c();
        std::iota(std::begin(span), std::end(span), 0);
    }
    
    void InFlatStruct (uint32_t value, nprpc::test::flat::FlatStruct_Direct a) override{
        EXPECT_EQ(value, 42);
        EXPECT_EQ(a.a(), 42);
        EXPECT_EQ(a.b(), 100u);
        EXPECT_EQ(a.c(), 3.14f);
    }

    void OutFlatStruct (uint32_t value, nprpc::test::flat::FlatStruct_Direct a) override{
        EXPECT_EQ(value, 42);
        a.a() = 42;
        a.b() = 100;
        a.c() = 3.14f;
    }

    void InStruct (nprpc::test::flat::AAA_Direct a) override{
        EXPECT_EQ(a.a(), 12345);
        EXPECT_EQ(std::string_view(a.b()), "Hello from InStruct");
        EXPECT_EQ(std::string_view(a.c()), "Another string");
    }

    void OutStruct (nprpc::test::flat::AAA_Direct a) override{
        a.a() = 12345;
        a.b("Hello from OutStruct");
        a.c("Another string");
    }

    void OutArrayOfStructs (::nprpc::flat::Vector_Direct2<nprpc::test::flat::SimpleStruct, nprpc::test::flat::SimpleStruct_Direct> a) override{
      a.length(10);
      auto span = a();
      int i = 1;
      for (auto s : span) {
        s.id() = i++;
      }
    }

    nprpc::test::IdArray ReturnIdArray () override {
      nprpc::test::IdArray arr {1,2,3,4,5,6,7,8,9,10};
      return arr;
    }

    void InException () override {
      throw nprpc::test::SimpleException{"This is a test exception", 123};
    }

    void OutScalarWithException(uint8_t dev_addr, uint16_t addr, uint8_t& value) override {
      // This tests the fix for flat output struct with exception handler
      // The output parameter 'value' must be declared before the try block
      // so it's in scope for both the function call and the assignment to output buffer
      
      // Simulate a simple read operation that succeeds
      value = static_cast<uint8_t>(dev_addr + addr); // Simple calculation for testing
    }
};