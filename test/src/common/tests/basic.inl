
class TestBasicImpl : public test::ITestBasic_Servant {
public:
    uint32_t ReturnU32() override{
        return 42;
    }

    bool ReturnBoolean() override{
        return true;
    }

    bool In(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) override {
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

    void OutStruct (test::flat::AAA_Direct& a) override{
        a.a() = 12345;
        a.b("Hello from OutStruct");
        a.c("Another string");
    }

    void OutArrayOfStructs (::nprpc::flat::Vector_Direct2<test::flat::SimpleStruct, test::flat::SimpleStruct_Direct> a) override{
      a.length(10);
      auto span = a();
      int i = 1;
      for (auto s : span) {
        s.id() = i++;
      }
    }

    test::IdArray ReturnIdArray () override {
      test::IdArray arr {1,2,3,4,5,6,7,8,9,10};
      return arr;
    }

    void InException () override {
      throw test::SimpleException{"This is a test exception", 123};
    }
};