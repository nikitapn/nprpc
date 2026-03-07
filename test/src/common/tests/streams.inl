
class TestStreamsImpl : public nprpc::test::ITestStreams_Servant {
public:
    std::mutex upload_mutex;
    std::condition_variable upload_cv;
    std::vector<uint8_t> uploaded_bytes;
    bool upload_done = false;

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

  ::nprpc::StreamWriter<nprpc::test::AAA> GetObjectStreamDirect(uint32_t count) override {
        // For testing, direct and non-direct can share the same implementation.
        // The generated code will handle the zero-copy path for the direct version.
        return GetObjectStream(count); 
    }

    void UploadByteStream(uint64_t expected_size, nprpc::StreamReader<uint8_t> data) override {
        std::vector<uint8_t> local;
        local.reserve(expected_size);
        {
            std::lock_guard lock(upload_mutex);
            upload_done = false;
            uploaded_bytes.clear();
        }
        for (auto& byte : data) {
            local.push_back(byte);
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_bytes = std::move(local);
            upload_done = true;
        }
        upload_cv.notify_all();
    }

    void EchoByteStream(uint8_t xor_mask, nprpc::BidiStream<uint8_t, uint8_t> stream) override {
        for (auto& byte : stream.reader) {
            stream.writer.write(static_cast<uint8_t>(byte ^ xor_mask));
        }
        stream.writer.close();
    }

    bool wait_for_upload(const std::vector<uint8_t>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return upload_done; })) {
            return false;
        }
        const bool matches = uploaded_bytes == expected;
        upload_done = false;
        return matches;
    }
};
