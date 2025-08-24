
class TestOptionalImpl : public test::ITestOptional_Servant {
public:
    virtual bool InEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
        EXPECT_FALSE(a.has_value());
        return true;
    }

    virtual bool In(::nprpc::flat::Optional_Direct<uint32_t> a, 
                   ::nprpc::flat::Optional_Direct<test::flat::AAA, test::flat::AAA_Direct> b) {
        EXPECT_TRUE(a.has_value());
        EXPECT_EQ(a.value(), 100u);
        EXPECT_TRUE(b.has_value());

        auto const& value = b.value();

        EXPECT_EQ(value.a(), 100u);
        EXPECT_EQ(std::string_view(value.b()), "test_b"sv);
        EXPECT_EQ(std::string_view(value.c()), "test_c"sv);

        return true;
    }

    virtual void OutEmpty(::nprpc::flat::Optional_Direct<uint32_t> a) {
        a.set_nullopt();
    }

    virtual void Out(::nprpc::flat::Optional_Direct<uint32_t> a) {
        a.alloc();
        a.value() = 100;
    }
};