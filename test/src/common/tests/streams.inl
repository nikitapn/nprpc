
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class TestStreamsImpl : public nprpc::test::ITestStreams_Servant {
public:
    std::mutex upload_mutex;
    std::condition_variable upload_cv;
    std::vector<uint8_t> uploaded_bytes;
    std::vector<std::string> uploaded_strings;
    std::vector<std::vector<uint8_t>> uploaded_binary_chunks;
    std::vector<std::vector<uint16_t>> uploaded_u16_vectors;
    std::vector<std::vector<nprpc::test::AAA>> uploaded_object_vectors;
    std::vector<std::array<uint16_t, 4>> uploaded_u16_arrays;
    std::vector<std::array<nprpc::test::AAA, 2>> uploaded_object_arrays;
    std::vector<nprpc::test::AAA> uploaded_objects;
    bool upload_done = false;
    bool string_upload_done = false;
    bool binary_upload_done = false;
    bool u16_vector_upload_done = false;
    bool object_vector_upload_done = false;
    bool u16_array_upload_done = false;
    bool object_array_upload_done = false;
    bool object_upload_done = false;

    static nprpc::test::AAA transform_aaa(nprpc::test::AAA value, const std::string& suffix) {
        value.a += 100;
        value.b += suffix;
        value.c += suffix;
        return value;
    }

    static nprpc::test::AliasOptionalStreamPayload transform_alias_optional_payload(
        nprpc::test::AliasOptionalStreamPayload value,
        const std::string& suffix,
        uint32_t delta) {
        value.id += delta;
        for (auto& item : value.ids) {
            item += delta;
        }
        for (auto& item : value.payload) {
            item = static_cast<uint8_t>(item ^ static_cast<uint8_t>(delta & 0xFF));
        }
        if (value.label) {
            value.label = value.label.value() + suffix;
        }
        if (value.item) {
            value.item = transform_aaa(std::move(value.item.value()), suffix);
        }
        if (value.maybe_id) {
            value.maybe_id = value.maybe_id.value() + delta;
        }
        if (value.maybe_ids) {
            auto& ids = value.maybe_ids.value();
            for (auto& item : ids) {
                item += delta;
            }
        }
        if (value.maybe_payload) {
            auto& payload = value.maybe_payload.value();
            for (auto& item : payload) {
                item = static_cast<uint8_t>(item ^ static_cast<uint8_t>(delta & 0xFF));
            }
        }
        return value;
    }

    // Server-side: return a StreamWriter coroutine that yields bytes
    nprpc::StreamWriter<uint8_t> GetByteStream(uint64_t size) override {
        // This is a coroutine that yields 'size' bytes
        for (uint64_t i = 0; i < size; ++i) {
            co_yield static_cast<uint8_t>(i & 0xFF);
        }
        // Coroutine ends naturally, triggering stream completion
    }

    nprpc::StreamWriter<std::string> GetStringStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            co_yield std::string("item_") + std::to_string(i);
        }
    }

    nprpc::StreamWriter<std::vector<uint8_t>> GetBinaryStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            std::vector<uint8_t> chunk{
                static_cast<uint8_t>(i),
                static_cast<uint8_t>(i + 1),
                static_cast<uint8_t>(i + 2),
            };
            co_yield std::move(chunk);
        }
    }

    nprpc::StreamWriter<nprpc::test::bytestream> GetAliasedBinaryStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            nprpc::test::bytestream chunk{
                static_cast<uint8_t>(i + 10),
                static_cast<uint8_t>(i + 11),
                static_cast<uint8_t>(i + 12),
            };
            co_yield std::move(chunk);
        }
    }

    nprpc::StreamWriter<std::vector<uint16_t>> GetU16VectorStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            std::vector<uint16_t> chunk{
                static_cast<uint16_t>(100 + i),
                static_cast<uint16_t>(200 + i),
                static_cast<uint16_t>(300 + i),
            };
            co_yield std::move(chunk);
        }
    }

    nprpc::StreamWriter<std::vector<nprpc::test::AAA>> GetObjectVectorStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            std::vector<nprpc::test::AAA> chunk{
                nprpc::test::AAA{.a = i * 10 + 1, .b = "vec_" + std::to_string(i) + "_0", .c = "payload_" + std::to_string(i) + "_0"},
                nprpc::test::AAA{.a = i * 10 + 2, .b = "vec_" + std::to_string(i) + "_1", .c = "payload_" + std::to_string(i) + "_1"},
            };
            co_yield std::move(chunk);
        }
    }

    nprpc::StreamWriter<std::array<uint16_t, 4>> GetU16ArrayStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            co_yield std::array<uint16_t, 4>{
                static_cast<uint16_t>(i),
                static_cast<uint16_t>(i + 10),
                static_cast<uint16_t>(i + 20),
                static_cast<uint16_t>(i + 30),
            };
        }
    }

    nprpc::StreamWriter<std::array<nprpc::test::AAA, 2>> GetObjectArrayStream(uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            co_yield std::array<nprpc::test::AAA, 2>{
                nprpc::test::AAA{.a = i * 10 + 1, .b = "arr_" + std::to_string(i) + "_0", .c = "item_" + std::to_string(i) + "_0"},
                nprpc::test::AAA{.a = i * 10 + 2, .b = "arr_" + std::to_string(i) + "_1", .c = "item_" + std::to_string(i) + "_1"},
            };
        }
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

    ::nprpc::Task<> UploadByteStream(uint64_t expected_size, nprpc::StreamReader<uint8_t> data) override {
        std::vector<uint8_t> local;
        local.reserve(expected_size);
        {
            std::lock_guard lock(upload_mutex);
            upload_done = false;
            uploaded_bytes.clear();
        }
        while (auto byte = co_await data) {
            local.push_back(*byte);
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_bytes = std::move(local);
            upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadObjectStream(uint64_t expected_count, nprpc::StreamReader<nprpc::test::AAA> data) override {
        std::vector<nprpc::test::AAA> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            object_upload_done = false;
            uploaded_objects.clear();
        }
        while (auto object = co_await data) {
            local.push_back(std::move(*object));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_objects = std::move(local);
            object_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadObjectStreamCanThrow(uint64_t expected_count, nprpc::StreamReader<nprpc::test::AAA> data) override {
        (void)expected_count;
        (void)data;
        throw nprpc::test::AssertionFailed{"UploadObjectStreamCanThrow rejected"};
        co_return;
    }

    ::nprpc::Task<> UploadStringStream(uint64_t expected_count, nprpc::StreamReader<std::string> data) override {
        std::vector<std::string> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            string_upload_done = false;
            uploaded_strings.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_strings = std::move(local);
            string_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadBinaryStream(uint64_t expected_count, nprpc::StreamReader<std::vector<uint8_t>> data) override {
        std::vector<std::vector<uint8_t>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            binary_upload_done = false;
            uploaded_binary_chunks.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_binary_chunks = std::move(local);
            binary_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadAliasedBinaryStream(uint64_t expected_count, nprpc::StreamReader<nprpc::test::bytestream> data) override {
        std::vector<std::vector<uint8_t>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            binary_upload_done = false;
            uploaded_binary_chunks.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_binary_chunks = std::move(local);
            binary_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadU16VectorStream(uint64_t expected_count, nprpc::StreamReader<std::vector<uint16_t>> data) override {
        std::vector<std::vector<uint16_t>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            u16_vector_upload_done = false;
            uploaded_u16_vectors.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_u16_vectors = std::move(local);
            u16_vector_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadObjectVectorStream(uint64_t expected_count, nprpc::StreamReader<std::vector<nprpc::test::AAA>> data) override {
        std::vector<std::vector<nprpc::test::AAA>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            object_vector_upload_done = false;
            uploaded_object_vectors.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_object_vectors = std::move(local);
            object_vector_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadU16ArrayStream(uint64_t expected_count, nprpc::StreamReader<std::array<uint16_t, 4>> data) override {
        std::vector<std::array<uint16_t, 4>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            u16_array_upload_done = false;
            uploaded_u16_arrays.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_u16_arrays = std::move(local);
            u16_array_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> UploadObjectArrayStream(uint64_t expected_count, nprpc::StreamReader<std::array<nprpc::test::AAA, 2>> data) override {
        std::vector<std::array<nprpc::test::AAA, 2>> local;
        local.reserve(static_cast<size_t>(expected_count));
        {
            std::lock_guard lock(upload_mutex);
            object_array_upload_done = false;
            uploaded_object_arrays.clear();
        }
        while (auto value = co_await data) {
            local.push_back(std::move(*value));
        }

        {
            std::lock_guard lock(upload_mutex);
            uploaded_object_arrays = std::move(local);
            object_array_upload_done = true;
        }
        upload_cv.notify_all();
        co_return;
    }

    ::nprpc::Task<> EchoByteStream(uint8_t xor_mask, nprpc::BidiStream<uint8_t, uint8_t> stream) override {
        while (auto byte = co_await stream.reader) {
            stream.writer.write(static_cast<uint8_t>(*byte ^ xor_mask));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoStringStream(std::string suffix, nprpc::BidiStream<std::string, std::string> stream) override {
        while (auto value = co_await stream.reader) {
            stream.writer.write(*value + suffix);
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoBinaryStream(uint8_t xor_mask, nprpc::BidiStream<std::vector<uint8_t>, std::vector<uint8_t>> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& byte : *value) {
                byte ^= xor_mask;
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoAliasedBinaryStream(uint8_t xor_mask, nprpc::BidiStream<nprpc::test::bytestream, nprpc::test::bytestream> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& byte : *value) {
                byte ^= xor_mask;
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoU16VectorStream(uint16_t delta, nprpc::BidiStream<std::vector<uint16_t>, std::vector<uint16_t>> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& item : *value) {
                item = static_cast<uint16_t>(item + delta);
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoObjectVectorStream(std::string suffix, nprpc::BidiStream<std::vector<nprpc::test::AAA>, std::vector<nprpc::test::AAA>> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& item : *value) {
                item.a += 100;
                item.b += suffix;
                item.c += suffix;
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoU16ArrayStream(uint16_t delta, nprpc::BidiStream<std::array<uint16_t, 4>, std::array<uint16_t, 4>> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& item : *value) {
                item = static_cast<uint16_t>(item + delta);
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoObjectArrayStream(std::string suffix, nprpc::BidiStream<std::array<nprpc::test::AAA, 2>, std::array<nprpc::test::AAA, 2>> stream) override {
        while (auto value = co_await stream.reader) {
            for (auto& item : *value) {
                item.a += 100;
                item.b += suffix;
                item.c += suffix;
            }
            stream.writer.write(std::move(*value));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoObjectToDifferentObjectStream(std::string suffix, nprpc::BidiStream<nprpc::test::AAA, nprpc::test::CCC> stream) override {
        while (auto object = co_await stream.reader) {
            nprpc::test::CCC response;
            response.a = object->b + suffix;
            response.b = object->c + suffix;
            response.c = std::optional<bool>{(object->a % 2u) == 0u};
            stream.writer.write(std::move(response));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoAliasOptionalStream(std::string suffix, uint32_t delta, nprpc::BidiStream<nprpc::test::AliasOptionalStreamPayload, nprpc::test::AliasOptionalStreamPayload> stream) override {
        while (auto value = co_await stream.reader) {
            stream.writer.write(transform_alias_optional_payload(std::move(*value), suffix, delta));
        }
        stream.writer.close();
        co_return;
    }

    ::nprpc::Task<> EchoObjectStream(std::string suffix, nprpc::BidiStream<nprpc::test::AAA, nprpc::test::AAA> stream) override {
        while (auto object = co_await stream.reader) {
            nprpc::test::AAA response;
            response.a = object->a + 100;
            response.b = object->b + suffix;
            response.c = object->c + suffix;
            stream.writer.write(std::move(response));
        }
        stream.writer.close();
        co_return;
    }

    static bool aaa_equal(const nprpc::test::AAA& lhs, const nprpc::test::AAA& rhs) {
        return lhs.a == rhs.a && lhs.b == rhs.b && lhs.c == rhs.c;
    }

    static bool aaa_vector_equal(const std::vector<nprpc::test::AAA>& lhs, const std::vector<nprpc::test::AAA>& rhs) {
        return lhs.size() == rhs.size() &&
            std::equal(lhs.begin(), lhs.end(), rhs.begin(), aaa_equal);
    }

    static bool aaa_array_equal(const std::array<nprpc::test::AAA, 2>& lhs, const std::array<nprpc::test::AAA, 2>& rhs) {
        return std::equal(lhs.begin(), lhs.end(), rhs.begin(), aaa_equal);
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

    bool wait_for_string_upload(const std::vector<std::string>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return string_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_strings == expected;
        string_upload_done = false;
        return matches;
    }

    bool wait_for_binary_upload(const std::vector<std::vector<uint8_t>>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return binary_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_binary_chunks == expected;
        binary_upload_done = false;
        return matches;
    }

    bool wait_for_u16_vector_upload(const std::vector<std::vector<uint16_t>>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return u16_vector_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_u16_vectors == expected;
        u16_vector_upload_done = false;
        return matches;
    }

    bool wait_for_object_vector_upload(const std::vector<std::vector<nprpc::test::AAA>>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return object_vector_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_object_vectors.size() == expected.size() &&
            std::equal(uploaded_object_vectors.begin(), uploaded_object_vectors.end(), expected.begin(),
                [](const auto& lhs, const auto& rhs) { return aaa_vector_equal(lhs, rhs); });
        object_vector_upload_done = false;
        return matches;
    }

    bool wait_for_u16_array_upload(const std::vector<std::array<uint16_t, 4>>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return u16_array_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_u16_arrays == expected;
        u16_array_upload_done = false;
        return matches;
    }

    bool wait_for_object_array_upload(const std::vector<std::array<nprpc::test::AAA, 2>>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return object_array_upload_done; })) {
            return false;
        }
        const bool matches = uploaded_object_arrays.size() == expected.size() &&
            std::equal(uploaded_object_arrays.begin(), uploaded_object_arrays.end(), expected.begin(),
                [](const auto& lhs, const auto& rhs) { return aaa_array_equal(lhs, rhs); });
        object_array_upload_done = false;
        return matches;
    }

    bool wait_for_object_upload(const std::vector<nprpc::test::AAA>& expected) {
        std::unique_lock lock(upload_mutex);
        if (!upload_cv.wait_for(lock, std::chrono::seconds(2), [this] { return object_upload_done; })) {
            return false;
        }
        const bool matches = aaa_vector_equal(uploaded_objects, expected);
        object_upload_done = false;
        return matches;
    }
};
