#include <random>
#include <thread>
#include <vector>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <gtest/gtest.h>

#include <nprpc/impl/lock_free_ring_buffer.hpp>
// visibility is hidden, so include the cpp directly
#include "../../nprpc/src/shm/lock_free_ring_buffer.cpp"

using namespace nprpc::impl;

static std::string random_string(size_t length)
{
  const char charset[] = "0123456789"
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

static const auto test_messages = ([]() -> std::vector<std::string> {
  constexpr size_t num_messages = 1000;
  std::vector<std::string> messages;
  for (size_t i = 0; i < num_messages; ++i)
    messages.push_back(random_string(std::rand() % 30 + 5));
  return messages;
})();

const std::string& get_test_message()
{
  static size_t index = 0;
  auto& msg = test_messages[index];
  index = (index + 1) % test_messages.size();
  return msg;
}

// Basic write and read test
TEST(LockFreeRingBuffer, WriteRead)
{
  // Slot format now carries 8 bytes of per-message metadata plus a spare byte
  // to distinguish full from empty, so keep the ring comfortably larger than
  // the generated test messages.
  auto buffer = LockFreeRingBuffer::create("test_ring_buffer", 64);

  ASSERT_NE(buffer, nullptr);

  size_t max_attempts = 1000000;
  while (max_attempts-- > 0) {
    // Write message
    const auto& message = get_test_message();
    size_t message_size = message.size();

    bool write_result = buffer->try_write(message.c_str(), message_size);
    ASSERT_TRUE(write_result);

    // Read message
    char read_buffer[64];
    size_t bytes_read = buffer->try_read(read_buffer, sizeof(read_buffer));
    ASSERT_EQ(bytes_read, message_size);
    ASSERT_EQ(std::string_view(read_buffer, bytes_read),
              std::string_view(message));
  }
}

// Test with 1MB payloads to match benchmark scenario
TEST(LockFreeRingBuffer, LargePayload1MB)
{
  // Use default 16MB buffer size
  auto buffer =
      LockFreeRingBuffer::create("test_ring_buffer_1mb", 16 * 1024 * 1024);
  ASSERT_NE(buffer, nullptr);

  // Test 1MB payload multiple times to force wraparound
  std::vector<uint8_t> write_data(1 * 1024 * 1024);
  std::vector<uint8_t> read_data(1 * 1024 * 1024);

  for (int i = 0; i < 20; ++i) {
    // Fill with pattern
    std::fill(write_data.begin(), write_data.end(),
              static_cast<uint8_t>(i & 0xFF));

    bool write_result = buffer->try_write(write_data.data(), write_data.size());
    ASSERT_TRUE(write_result) << "Failed to write 1MB on iteration " << i;

    size_t bytes_read = buffer->try_read(read_data.data(), read_data.size());
    ASSERT_EQ(bytes_read, write_data.size())
        << "Read size mismatch on iteration " << i;
    ASSERT_EQ(read_data, write_data) << "Data corruption on iteration " << i;
  }
}

// Test wraparound at buffer boundary
TEST(LockFreeRingBuffer, Wraparound)
{
  size_t buffer_size = 1024; // Small buffer to force wraparound
  auto buffer =
      LockFreeRingBuffer::create("test_ring_buffer_wrap", buffer_size);
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

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// ─────────────────────────────────────────────────────────────────────────────
// Zero-copy API: try_reserve_write / commit_write / try_read_view / commit_read
// ─────────────────────────────────────────────────────────────────────────────

TEST(LockFreeRingBuffer, ZeroCopyBasic)
{
  auto ring = LockFreeRingBuffer::create("test_zc_basic", 4096);
  ASSERT_NE(ring, nullptr);

  const std::string msg = "hello zero-copy";
  const size_t msg_len  = msg.size();

  // Write via zero-copy reserve/commit.
  auto rsv = ring->try_reserve_write(msg_len);
  ASSERT_TRUE(rsv) << "try_reserve_write failed";
  ASSERT_NE(rsv.data, nullptr);
  ASSERT_GE(rsv.max_size, msg_len);
  std::memcpy(rsv.data, msg.data(), msg_len);
  ring->commit_write(rsv, msg_len);

  // Read via zero-copy view/commit.
  auto view = ring->try_read_view();
  ASSERT_TRUE(view) << "try_read_view returned nothing";
  ASSERT_EQ(view.size, msg_len);
  ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(view.data), view.size), msg);
  ring->commit_read(view);

  // Ring should be empty afterwards.
  auto empty = ring->try_read_view();
  ASSERT_FALSE(empty) << "ring should be empty after commit_read";
}

TEST(LockFreeRingBuffer, ZeroCopyMultipleMessages)
{
  auto ring = LockFreeRingBuffer::create("test_zc_multi", 64 * 1024);
  ASSERT_NE(ring, nullptr);

  const std::vector<std::string> messages = {
      "first",
      "second message is longer",
      "third",
      std::string(500, 'x'),
      "fifth and last",
  };

  // Write all messages.
  for (const auto& m : messages) {
    auto rsv = ring->try_reserve_write(m.size());
    ASSERT_TRUE(rsv) << "reserve failed for '" << m << "'";
    std::memcpy(rsv.data, m.data(), m.size());
    ring->commit_write(rsv, m.size());
  }

  // Read them back in order.
  for (const auto& expected : messages) {
    auto view = ring->try_read_view();
    ASSERT_TRUE(view) << "expected message '" << expected << "' missing";
    ASSERT_EQ(view.size, expected.size());
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(view.data), view.size),
              expected);
    ring->commit_read(view);
  }

  ASSERT_FALSE(ring->try_read_view()) << "ring should be empty";
}

TEST(LockFreeRingBuffer, ZeroCopyWraparound)
{
  // Small ring (~1 KB) — messages of ~200 bytes force multiple laps.
  auto ring = LockFreeRingBuffer::create("test_zc_wrap", 1024);
  ASSERT_NE(ring, nullptr);

  const size_t payload_size = 200;
  std::vector<uint8_t> write_buf(payload_size);
  const int iterations = 20; // >>5 full laps

  for (int i = 0; i < iterations; ++i) {
    std::fill(write_buf.begin(), write_buf.end(), static_cast<uint8_t>(i & 0xFF));

    auto rsv = ring->try_reserve_write(payload_size);
    ASSERT_TRUE(rsv) << "reserve failed at iteration " << i;
    std::memcpy(rsv.data, write_buf.data(), payload_size);
    ring->commit_write(rsv, payload_size);

    auto view = ring->try_read_view();
    ASSERT_TRUE(view) << "read_view failed at iteration " << i;
    ASSERT_EQ(view.size, payload_size);
    for (size_t j = 0; j < payload_size; ++j)
      ASSERT_EQ(view.data[j], static_cast<uint8_t>(i & 0xFF))
          << "data corruption at iteration " << i << " byte " << j;
    ring->commit_read(view);
  }
}

// Simulates the actual ShmEgressFrame write + read pattern used in
// Http3Server → npquicrouter to make sure the framing is correct.
#pragma pack(push, 1)
struct TestShmEgressFrame {
  uint32_t payload_len;
  uint16_t gso_segment_size;
  uint8_t  ep_len;
  uint8_t  _pad;
  uint8_t  ep_storage[28];
};
#pragma pack(pop)
static_assert(sizeof(TestShmEgressFrame) == 36,
              "TestShmEgressFrame size mismatch");

TEST(LockFreeRingBuffer, ZeroCopyShmEgressFrameLayout)
{
  auto ring = LockFreeRingBuffer::create("test_zc_egress", 32 * 1024 * 1024);
  ASSERT_NE(ring, nullptr);

  // Simulate several differently-sized UDP payloads.
  const std::vector<size_t> payload_sizes = {52, 1200, 1200, 56, 1200, 58, 1200};
  const uint16_t gso = 1200;

  for (size_t idx = 0; idx < payload_sizes.size(); ++idx) {
    const size_t plen      = payload_sizes[idx];
    const size_t msg_size  = sizeof(TestShmEgressFrame) + plen;

    auto rsv = ring->try_reserve_write(msg_size);
    ASSERT_TRUE(rsv) << "reserve failed for payload " << plen;

    TestShmEgressFrame hdr{};
    hdr.payload_len      = static_cast<uint32_t>(plen);
    hdr.gso_segment_size = (plen > gso) ? gso : 0;
    hdr.ep_len           = sizeof(sockaddr_in); // 16
    // Fake sockaddr_in: 127.0.0.1:40184
    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port   = htons(40184);
    sin.sin_addr.s_addr = htonl(0x7f000001); // 127.0.0.1
    std::memcpy(hdr.ep_storage, &sin, sizeof(sin));

    std::memcpy(rsv.data, &hdr, sizeof(hdr));
    // Fill payload with a recognisable pattern.
    std::memset(rsv.data + sizeof(hdr), static_cast<uint8_t>(idx & 0xFF), plen);
    ring->commit_write(rsv, msg_size);
  }

  // Read back and verify.
  for (size_t idx = 0; idx < payload_sizes.size(); ++idx) {
    const size_t plen = payload_sizes[idx];

    auto view = ring->try_read_view();
    ASSERT_TRUE(view) << "read_view missing at idx=" << idx;
    ASSERT_GE(view.size, sizeof(TestShmEgressFrame)) << "frame too short";

    TestShmEgressFrame hdr{};
    std::memcpy(&hdr, view.data, sizeof(hdr));

    ASSERT_EQ(hdr.payload_len, plen)             << "payload_len mismatch at idx=" << idx;
    ASSERT_EQ(hdr.ep_len, sizeof(sockaddr_in))   << "ep_len mismatch at idx=" << idx;
    ASSERT_EQ(view.size, sizeof(TestShmEgressFrame) + plen)
        << "total size mismatch at idx=" << idx;

    const uint8_t* payload = view.data + sizeof(hdr);
    for (size_t j = 0; j < plen; ++j)
      ASSERT_EQ(payload[j], static_cast<uint8_t>(idx & 0xFF))
          << "payload corruption at idx=" << idx << " byte=" << j;

    ring->commit_read(view);
  }

  ASSERT_FALSE(ring->try_read_view()) << "ring should be empty";
}

// Producer thread writes N messages; consumer thread reads them back and
// checks ordering and data integrity.
TEST(LockFreeRingBuffer, ZeroCopyConcurrentMPSC)
{
  constexpr size_t kMessages       = 2000;
  constexpr size_t kPayload        = 128;
  constexpr size_t kProducerCount  = 4;
  constexpr size_t kMsgsPerProd    = kMessages / kProducerCount;

  auto ring = LockFreeRingBuffer::create("test_zc_mpsc", 4 * 1024 * 1024);
  ASSERT_NE(ring, nullptr);

  std::atomic<size_t> consumed{0};

  // Consumer thread: reads until all messages accounted for.
  std::thread consumer([&] {
    size_t got = 0;
    while (got < kMessages) {
      auto view = ring->try_read_view();
      if (!view) { std::this_thread::yield(); continue; }
      ASSERT_EQ(view.size, kPayload);
      ring->commit_read(view);
      ++got;
    }
    consumed.store(got, std::memory_order_release);
  });

  // Producer threads: each writes kMsgsPerProd messages.
  std::vector<std::thread> producers;
  for (size_t p = 0; p < kProducerCount; ++p) {
    producers.emplace_back([&, p] {
      std::vector<uint8_t> buf(kPayload, static_cast<uint8_t>(p));
      for (size_t i = 0; i < kMsgsPerProd; ++i) {
        LockFreeRingBuffer::WriteReservation rsv;
        while (!(rsv = ring->try_reserve_write(kPayload)))
          std::this_thread::yield();
        std::memcpy(rsv.data, buf.data(), kPayload);
        ring->commit_write(rsv, kPayload);
      }
    });
  }

  for (auto& t : producers) t.join();
  consumer.join();

  ASSERT_EQ(consumed.load(), kMessages);
  ASSERT_FALSE(ring->try_read_view()) << "ring should be empty after all reads";
}
