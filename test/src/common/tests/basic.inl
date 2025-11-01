
class TestBasicImpl : public test::ITestBasic_Servant {
public:
    virtual uint32_t ReturnU32() {
        return 42;
    }

    virtual bool ReturnBoolean() {
        return true;
    }

    virtual bool In(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) {
        EXPECT_EQ(a, 100u);
        EXPECT_TRUE(b.get());

        uint8_t ix = 0;
        for (auto i : c) {
            EXPECT_EQ(ix++, i);
        }

        return true;
    }

    virtual void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) {
        a = 100;
        b = true;

        c.length(256);
        auto span = c();
        std::iota(std::begin(span), std::end(span), 0);
    }

    virtual void OutStruct (test::flat::AAA_Direct& a) {
        a.a() = 12345;
        a.b("Hello from OutStruct");
        a.c("Another string");
    }

    virtual void OutArrayOfStructs (::nprpc::flat::Vector_Direct2<test::flat::SimpleStruct, test::flat::SimpleStruct_Direct> a) {
      a.length(10);
      auto span = a();
      int i = 1;
      for (auto s : span) {
        s.id() = i++;
      }
    }

    virtual test::IdArray ReturnIdArray () {
      test::IdArray arr {1,2,3,4,5,6,7,8,9,10};
      return arr;
    }
};