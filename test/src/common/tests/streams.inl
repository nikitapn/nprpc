
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

    // Server-side: yield 'count' AAA objects
    nprpc::StreamWriter<nprpc::test::AAA> GetObjectStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            nprpc::test::AAA obj;
            obj.a = i;
            obj.b = "name_" + std::to_string(i);
            obj.c = "value_" + std::to_string(i);
            co_yield std::move(obj);
        }
    }
};
