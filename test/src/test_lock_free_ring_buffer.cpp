#include <random>

#include <gtest/gtest.h>

#include <nprpc/impl/lock_free_ring_buffer.hpp>
// visibility is hidden, so include the cpp directly
#include "../../nprpc/src/shm/lock_free_ring_buffer.cpp"

using namespace nprpc::impl;


static std::string random_string(size_t length) {
  const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
  std::default_random_engine rng(std::random_device{}());
  std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
  
  std::string result;
  result.resize(length);
  for (size_t i = 0; i < length; ++i) {
    result[i] = charset[dist(rng)];
  }
  return result;
}

static const auto test_messages = ([]()->std::vector<std::string> {
    constexpr size_t num_messages = 1000;
    std::vector<std::string> messages;
    for (size_t i = 0; i < num_messages; ++i)
      messages.push_back(random_string(std::rand() % 30 + 5));
    return messages;
})();

const std::string& get_test_message() {
    static size_t index = 0;
    auto& msg = test_messages[index];
    index = (index + 1) % test_messages.size();
    return msg;
}

// Basic write and read test
TEST(LockFreeRingBuffer, WriteRead) {
  auto buffer = LockFreeRingBuffer::create("test_ring_buffer", 40);

  ASSERT_NE(buffer, nullptr);

  size_t max_attempts = 1000000;
  while (max_attempts-- > 0) {
    // Write message
    const auto& message = get_test_message();
    size_t message_size = message.size();

    bool write_result = buffer->try_write(message.c_str(), message_size);
    ASSERT_TRUE(write_result);

    // Read message
    char read_buffer[40];
    size_t bytes_read = buffer->try_read(read_buffer, sizeof(read_buffer));
    ASSERT_EQ(bytes_read, message_size);
    ASSERT_EQ(std::string_view(read_buffer, bytes_read), std::string_view(message));
  }
}

// Test with 1MB payloads to match benchmark scenario
TEST(LockFreeRingBuffer, LargePayload1MB) {
  // Use default 16MB buffer size
  auto buffer = LockFreeRingBuffer::create("test_ring_buffer_1mb", 16 * 1024 * 1024);
  ASSERT_NE(buffer, nullptr);

  // Test 1MB payload multiple times to force wraparound
  std::vector<uint8_t> write_data(1 * 1024 * 1024);
  std::vector<uint8_t> read_data(1 * 1024 * 1024);
  
  for (int i = 0; i < 20; ++i) {
    // Fill with pattern
    std::fill(write_data.begin(), write_data.end(), static_cast<uint8_t>(i & 0xFF));
    
    bool write_result = buffer->try_write(write_data.data(), write_data.size());
    ASSERT_TRUE(write_result) << "Failed to write 1MB on iteration " << i;
    
    size_t bytes_read = buffer->try_read(read_data.data(), read_data.size());
    ASSERT_EQ(bytes_read, write_data.size()) << "Read size mismatch on iteration " << i;
    ASSERT_EQ(read_data, write_data) << "Data corruption on iteration " << i;
  }
}

// Test wraparound at buffer boundary
TEST(LockFreeRingBuffer, Wraparound) {
  size_t buffer_size = 1024; // Small buffer to force wraparound
  auto buffer = LockFreeRingBuffer::create("test_ring_buffer_wrap", buffer_size);
  ASSERT_NE(buffer, nullptr);

  std::vector<uint8_t> data(500); // ~50% of buffer size
  std::vector<uint8_t> read_buf(500);
  
  // Write enough to cause wraparound
  for (int i = 0; i < 10; ++i) {
    std::fill(data.begin(), data.end(), static_cast<uint8_t>(i));
    
    bool write_result = buffer->try_write(data.data(), data.size());
    ASSERT_TRUE(write_result) << "Write failed on iteration " << i;
    
    size_t bytes_read = buffer->try_read(read_buf.data(), read_buf.size());
    ASSERT_EQ(bytes_read, data.size());
    ASSERT_EQ(read_buf, data) << "Wraparound corruption on iteration " << i;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
