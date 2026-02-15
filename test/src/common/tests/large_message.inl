class TestLargeMessage : public nprpc::test::ITestLargeMessage_Servant {
public:
    virtual bool ReturnBoolean() {
        return true;
    }

    virtual bool In_(uint32_t a, ::nprpc::flat::Boolean b, ::nprpc::flat::Span<uint8_t> c) {
        EXPECT_EQ(a, 42u);
        EXPECT_TRUE(b.get());

        // Verify we received exactly 3MB of data
        EXPECT_EQ(c.size(), 3 * 1024 * 1024u);

        // Verify pattern in first and last bytes
        EXPECT_EQ(c[0], 0xAB);
        EXPECT_EQ(c[c.size() - 1], 0xCD);

        return true;
    }

    virtual void Out(uint32_t& a, ::nprpc::flat::Boolean& b, ::nprpc::flat::Vector_Direct1<uint8_t> c) {
        a = 42;
        b = true;

        // Create 3MB of test data
        const size_t large_size = 3 * 1024 * 1024; // 3MB
        c.length(large_size);
        auto span = c();
        
        // Fill with test pattern - first byte 0xAB, last byte 0xCD, middle with sequence
        span[0] = 0xAB;
        span[large_size - 1] = 0xCD;
        for (size_t i = 1; i < large_size - 1; ++i) {
            span[i] = static_cast<uint8_t>(i % 256);
        }
    }
};