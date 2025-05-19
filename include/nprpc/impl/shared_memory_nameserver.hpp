#pragma once

#include <boost/uuid/uuid.hpp>
#include <array>
#include <atomic>

namespace nprpc::impl {

struct ChannelDescription {
    std::array<char, 128> s2c_name;
    std::array<char, 128> c2s_name;
    std::array<char, 128> event_read;
    std::array<char, 128> event_write;
    bool in_use;
    uint32_t last_heartbeat;
};

// This structure is stored in well-known shared memory location
struct SharedMemoryNameServer {
    static constexpr const char* NAMESERVER_NAME = 
#ifdef _WIN32
        "Local\\nprpc_nameserver";
#else
        "/nprpc_nameserver";
#endif
    
    static constexpr uint32_t MAX_CHANNELS = 32;
    static constexpr uint32_t MAGIC = 0x4E505243; // "NPRC"

    uint32_t magic;
    std::atomic<uint32_t> server_pid;
    std::atomic<uint32_t> channel_count;
    std::atomic<bool> server_ready;
    ChannelDescription channels[MAX_CHANNELS];

#ifdef _WIN32
    char mutex_name[128];  // Windows named mutex
#else
    std::atomic<int> mutex; // pthread mutex in shared memory
#endif
};
