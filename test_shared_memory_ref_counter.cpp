#include <nprpc/impl/shared_memory_ref_counter.hpp>
#include <nprpc/impl/shared_ring_buffer.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <future>

using namespace nprpc::impl;

// Example of how to use the robust shared memory reference counting

class SharedMemoryRPCChannel {
private:
    SharedMemoryRef<SharedRingBuffer> send_buffer_;
    SharedMemoryRef<SharedRingBuffer> recv_buffer_;
    std::string channel_name_;

public:
    SharedMemoryRPCChannel(const std::string& channel_name, bool is_server) 
        : channel_name_(channel_name) {
        
        std::string send_name = channel_name + (is_server ? "_s2c" : "_c2s");
        std::string recv_name = channel_name + (is_server ? "_c2s" : "_s2c");
        
        // Create cleanup functions that will be called when last reference is removed
        auto send_cleanup = [send_name]() {
            std::cout << "Cleaning up send buffer: " << send_name << std::endl;
            // Additional cleanup logic here (close semaphores, etc.)
        };
        
        auto recv_cleanup = [recv_name]() {
            std::cout << "Cleaning up recv buffer: " << recv_name << std::endl;
            // Additional cleanup logic here
        };
        
        try {
            // Try to create or attach to shared memory buffers
            send_buffer_ = create_or_attach_buffer(send_name, send_cleanup);
            recv_buffer_ = create_or_attach_buffer(recv_name, recv_cleanup);
            
            std::cout << "Channel " << channel_name << " initialized successfully" << std::endl;
            std::cout << "Send buffer ref count: " << send_buffer_.ref_count() << std::endl;
            std::cout << "Recv buffer ref count: " << recv_buffer_.ref_count() << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize channel " << channel_name 
                      << ": " << e.what() << std::endl;
            throw;
        }
    }
    
    ~SharedMemoryRPCChannel() {
        std::cout << "Destroying channel " << channel_name_ << std::endl;
        // send_buffer_ and recv_buffer_ destructors will automatically
        // decrement reference counts and cleanup if needed
    }
    
    void send_message(const std::string& message) {
        if (send_buffer_) {
            // Use the buffer to send message
            std::cout << "Sending message via " << channel_name_ 
                      << " (ref count: " << send_buffer_.ref_count() << "): " 
                      << message << std::endl;
        }
    }
    
    std::string receive_message() {
        if (recv_buffer_) {
            // Use the buffer to receive message
            std::cout << "Receiving message via " << channel_name_ 
                      << " (ref count: " << recv_buffer_.ref_count() << ")" << std::endl;
            return "received_message";
        }
        return "";
    }

private:
    SharedMemoryRef<SharedRingBuffer> create_or_attach_buffer(
        const std::string& buffer_name, 
        SharedMemoryRefCounter::CleanupFunction cleanup_func) {
        
        // For this example, we'll create a dummy SharedRingBuffer
        // In real implementation, this would create actual shared memory
        auto* buffer = new SharedRingBuffer(); // This should be in shared memory
        
        return SharedMemoryRef<SharedRingBuffer>(buffer_name, buffer, cleanup_func);
    }
};

// Test function demonstrating multi-process scenario simulation
void test_multi_process_simulation() {
    std::cout << "\n=== Testing Multi-Process Simulation ===" << std::endl;
    
    std::vector<std::future<void>> futures;
    
    // Simulate multiple "processes" (threads) attaching to same shared memory
    for (int i = 0; i < 3; ++i) {
        futures.emplace_back(std::async(std::launch::async, [i]() {
            try {
                std::cout << "Process " << i << " starting..." << std::endl;
                
                // Each "process" creates a channel - they should all share the same buffers
                auto channel = std::make_unique<SharedMemoryRPCChannel>("test_channel", i == 0);
                
                // Simulate some work
                channel->send_message("Hello from process " + std::to_string(i));
                std::this_thread::sleep_for(std::chrono::milliseconds(100 * (i + 1)));
                channel->receive_message();
                
                std::cout << "Process " << i << " finishing..." << std::endl;
                
            } catch (const std::exception& e) {
                std::cerr << "Process " << i << " error: " << e.what() << std::endl;
            }
        }));
    }
    
    // Wait for all "processes" to complete
    for (auto& future : futures) {
        future.get();
    }
    
    std::cout << "All processes completed" << std::endl;
}

// Test function for reference counter reliability
void test_reference_counter_reliability() {
    std::cout << "\n=== Testing Reference Counter Reliability ===" << std::endl;
    
    bool cleanup_called = false;
    
    {
        // Create first reference
        SharedMemoryRefCounter counter1("test_counter", [&cleanup_called]() {
            cleanup_called = true;
            std::cout << "Cleanup function called!" << std::endl;
        });
        
        std::cout << "Initial ref count: " << counter1.get_count() << std::endl;
        
        {
            // Create second reference by moving
            auto counter2 = std::move(counter1);
            std::cout << "After move, ref count: " << counter2.get_count() << std::endl;
            
            // Create third reference by incrementing
            counter2.increment();
            std::cout << "After increment, ref count: " << counter2.get_count() << std::endl;
            
            // counter2 goes out of scope here, should decrement count
        }
        
        std::cout << "After counter2 destruction, cleanup called: " << cleanup_called << std::endl;
        
        // counter1 was moved, so it shouldn't affect the count when it goes out of scope
    }
    
    std::cout << "After all destructors, cleanup called: " << cleanup_called << std::endl;
}

int main() {
    try {
        test_reference_counter_reliability();
        test_multi_process_simulation();
        
        std::cout << "\nAll tests completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
