#include <gtest/gtest.h>
#include <nprpc/endpoint.hpp>

using namespace nprpc;

TEST(SharedMemoryEndpoint, ParsesCorrectly) {
    // Test shared memory endpoint without port
    EndPoint ep1("mem://channel_12345");
    EXPECT_EQ(ep1.type(), EndPointType::SharedMemory);
    EXPECT_EQ(ep1.memory_channel_id(), "channel_12345");
    EXPECT_EQ(ep1.hostname(), "channel_12345");
    EXPECT_EQ(ep1.port(), 0);
    EXPECT_FALSE(ep1.empty());
}

TEST(SharedMemoryEndpoint, ParsesWithOptionalPort) {
    // Test shared memory endpoint with port (optional, for compatibility)
    EndPoint ep2("mem://some_uuid_here:0");
    EXPECT_EQ(ep2.type(), EndPointType::SharedMemory);
    EXPECT_EQ(ep2.memory_channel_id(), "some_uuid_here");
    EXPECT_EQ(ep2.hostname(), "some_uuid_here");
    EXPECT_EQ(ep2.port(), 0);
}

TEST(SharedMemoryEndpoint, ToStringFormat) {
    // Test to_string() output
    EndPoint ep3("mem://test_channel_abc");
    EXPECT_EQ(ep3.to_string(), "mem://test_channel_abc");
    
    // Should not include port separator after channel ID
    std::string str = ep3.to_string();
    size_t prefix_end = str.find("//") + 2;  // Skip the "mem://" prefix
    EXPECT_EQ(str.find(':', prefix_end), std::string::npos);
}

TEST(SharedMemoryEndpoint, GetFullReturnsChannelId) {
    // Test get_full() returns just the channel ID for shared memory
    EndPoint ep4("mem://my_channel_xyz");
    EXPECT_EQ(ep4.get_full(), "my_channel_xyz");
}

TEST(SharedMemoryEndpoint, ComparisonOperators) {
    // Test equality operators
    EndPoint ep5("mem://channel_1");
    EndPoint ep6("mem://channel_1");
    EndPoint ep7("mem://channel_2");
    
    EXPECT_EQ(ep5, ep6);
    EXPECT_NE(ep5, ep7);
}

TEST(SharedMemoryEndpoint, ConstructorWithParameters) {
    // Test constructor with explicit parameters
    EndPoint ep8(EndPointType::SharedMemory, "explicit_channel", 0);
    EXPECT_EQ(ep8.type(), EndPointType::SharedMemory);
    EXPECT_EQ(ep8.memory_channel_id(), "explicit_channel");
    EXPECT_EQ(ep8.to_string(), "mem://explicit_channel");
}

TEST(SharedMemoryEndpoint, EmptyChannelId) {
    // Test that an endpoint with empty hostname reports empty
    EndPoint ep9;
    EXPECT_TRUE(ep9.empty());
    EXPECT_TRUE(ep9.memory_channel_id().empty());
}

// Test that other endpoint types return empty memory_channel_id
TEST(SharedMemoryEndpoint, OtherTypesReturnEmpty) {
    EndPoint tcp_ep("tcp://localhost:8080");
    EXPECT_TRUE(tcp_ep.memory_channel_id().empty());
    
    EndPoint ws_ep("ws://example.com:9090");
    EXPECT_TRUE(ws_ep.memory_channel_id().empty());
}

// Test UUID-like channel IDs
TEST(SharedMemoryEndpoint, UuidChannelIds) {
    std::string uuid = "550e8400-e29b-41d4-a716-446655440000";
    EndPoint ep10("mem://" + uuid);
    EXPECT_EQ(ep10.memory_channel_id(), uuid);
    EXPECT_EQ(ep10.to_string(), "mem://" + uuid);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
