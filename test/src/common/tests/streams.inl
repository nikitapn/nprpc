
class TestStreamsImpl : public nprpc::test::ITestStreams_Servant {
public:
    // Server-side: return a StreamWriter coroutine that yields bytes
    nprpc::StreamWriter<uint8_t> GetByteStream(uint64_t size) override {
        // This is a coroutine that yields 'size' bytes
        for (uint64_t i = 0; i < size; ++i) {
            co_yield static_cast<uint8_t>(i & 0xFF);
        }
        // Coroutine ends naturally, triggering stream completion
    }
};
