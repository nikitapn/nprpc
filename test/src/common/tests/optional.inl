
class TestOptionalImpl : public nprpc::test::ITestOptional_Servant {
public:
    bool InEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) override {
        EXPECT_FALSE(a.has_value());
        return true;
    }

    bool In(::nprpc::flat::Optional_Direct<uint32_t> a, 
                   ::nprpc::flat::Optional_Direct<nprpc::test::flat::AAA, nprpc::test::flat::AAA_Direct> b) override {
        EXPECT_TRUE(a.has_value());
        EXPECT_EQ(a.value(), 100u);
        EXPECT_TRUE(b.has_value());

        auto const& value = b.value();

        EXPECT_EQ(value.a(), 100u);
        EXPECT_EQ(std::string_view(value.b()), "test_b"sv);
        EXPECT_EQ(std::string_view(value.c()), "test_c"sv);

        return true;
    }

    void OutEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) override {
        a.set_nullopt();
    }

    void Out(::nprpc::flat::Optional_Direct<uint32_t> a) override {
        a.alloc();
        a.value() = 100;
    }

    nprpc::test::Opt1 ReturnOpt1() override {
        nprpc::test::Opt1 ret;
        ret.str = "test_string";
        ret.data = std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        return ret;
    }
};