## Shared Memory Transport Architecture

### Overview
Shared memory transport provides zero-copy IPC for objects on the same machine. Each server process has ONE listener channel (UUID) that all its objects share.

### Design

#### **Server Side** (Server Startup)
```cpp
// When server process starts
auto listener_uuid = generate_uuid();  // One per server process
auto listener = std::make_shared<SharedMemoryListener>(
    ioc, 
    listener_uuid,
    [](std::unique_ptr<SharedMemoryChannel> channel) {
        // Accept handler - create session for this connection
        auto session = std::make_shared<SharedMemoryServerSession>(
            std::move(channel));
        sessions.push_back(session);
    });
listener->start();

// Store listener_uuid for this process
g_server_listener_uuid = listener_uuid;
```

#### **Server Side** (Object Activation)
```cpp
// When POA activates an object with ALLOW_SHARED_MEMORY flag
ObjectId oid;
oid.origin = get_machine_uuid();  // Machine0

// Add shared memory URL (points to server's listener)
if (flags & ALLOW_SHARED_MEMORY) {
    oid.urls.push_back("mem://" + g_server_listener_uuid);
}

// Add network URLs as fallback
oid.urls.push_back("tcp://192.168.1.100:8080");
oid.urls.push_back("ws://example.com:9090");
```

#### **Client Side** (Object Connection)
```cpp
// Client receives ObjectId (from nameserver or direct reference)
if (object_id.origin == local_origin) {
    // Same machine! Check for shared memory URL
    for (const auto& url : object_id.urls) {
        if (url.starts_with("mem://")) {
            // Connect to server's listener
            EndPoint ep(url);  // "mem://550e8400-e29b-41d4-a716-446655440000"
            connection = get_session(ep);  // This does the handshake
            break;
        }
    }
}
// If no mem:// URL or different origin, fall back to tcp:// or ws://
```

### Protocol

#### **Server Startup**
1. Generate **listener UUID** (one per server process)
2. Create `SharedMemoryListener` with UUID
   - Creates accept queue: `<listener_uuid>` (well-known for this process)
3. Start accept loop
4. Store `listener_uuid` globally for object activation

#### **Object Activation** (Server)
1. Create object in POA
2. Check if `ALLOW_SHARED_MEMORY` flag is set
3. If yes, add `"mem://<listener_uuid>"` to `ObjectId.urls`
4. Add network URLs as fallback (`tcp://`, `ws://`)
5. Publish ObjectId (nameserver or direct)

#### **Connection Establishment** (Client)
1. Receive ObjectId
2. Check `ObjectId.origin == local_machine_uuid`
3. If same machine, look for `mem://` URL
4. Connect to listener:
   - Generate UUID for dedicated channel
   - Send handshake to listener's accept queue
   - Wait for server to create dedicated channel
   - Open dedicated channel queues
5. Use dedicated channel for all RPC calls to that object

#### **Handshake Flow**
```
Client                          Server (Listener)
  |                                    |
  |-- Handshake(channel_uuid) -------> | (via accept queue)
  |                                    |
  |                                    |-- Creates dedicated channel
  |                                    |   - <uuid>_s2c queue
  |                                    |   - <uuid>_c2s queue
  |                                    |
  |<--- Handshake(channel_uuid) ------ | (via dedicated channel)
  |                                    |
  |=== RPC on dedicated channel ====== |
```

### Key Points

✅ **One Listener Per Server Process**: All objects share the same listener UUID  
✅ **Dedicated Channels Per Connection**: After handshake, each client gets its own channel  
✅ **UUID-based**: Both listener and channels identified by UUIDs  
✅ **Automatic Cleanup**: Listener removed on server shutdown, channels on connection close  
✅ **Origin Check**: Client only uses shared memory if `ObjectId.origin == local_origin`  
✅ **Fallback**: If shared memory fails, client falls back to TCP/WebSocket  
✅ **Multiple Objects**: Client can call multiple objects through same connection  

### Comparison with TCP

| Aspect | TCP | Shared Memory |
|--------|-----|---------------|
| Discovery | DNS/IP:port | ObjectId.urls |
| Listener | One per server (port) | One per server (UUID) |
| Connection | connect() then handshake | Handshake then dedicated channel |
| Per-connection | Socket | Dedicated message queue pair |
| Multiple objects | Reuse socket | Reuse connection |
| Cleanup | Close socket | Remove queues |

### Example Flow

```
ServerA Process (Machine0):
  1. Start: Generate listener UUID = "listener-aaa-111"
  2. Create SharedMemoryListener("listener-aaa-111")
  3. Activate ObjectA with ALLOW_SHARED_MEMORY
     - ObjectA.origin = <Machine0 UUID>
     - ObjectA.urls = ["mem://listener-aaa-111", "tcp://192.168.1.10:8080"]
  4. Activate ObjectB with ALLOW_SHARED_MEMORY
     - ObjectB.origin = <Machine0 UUID>
     - ObjectB.urls = ["mem://listener-aaa-111", "tcp://192.168.1.10:8080"]
  5. Register objects with nameserver

ServerB Process (Machine0):
  1. Start: Generate listener UUID = "listener-bbb-222"  
  2. Create SharedMemoryListener("listener-bbb-222")
  3. Activate ObjectC with ALLOW_SHARED_MEMORY
     - ObjectC.origin = <Machine0 UUID>
     - ObjectC.urls = ["mem://listener-bbb-222", "tcp://192.168.1.11:8080"]
  4. Register objects with nameserver

Client Process (Machine0):
  1. Query nameserver for "ObjectA"
  2. Receive ObjectA.ObjectId
  3. Compare: ObjectA.origin == local_origin ✓ (same machine!)
  4. Find "mem://listener-aaa-111" in urls
  5. Connect to listener (handshake establishes dedicated channel)
  6. Make RPC calls to ObjectA (zero-copy!)
  7. Query nameserver for "ObjectB"
  8. Receive ObjectB.ObjectId - same listener URL!
  9. Reuse existing connection to make calls to ObjectB

Client Process (Machine1):
  1. Query nameserver for "ObjectA"
  2. Receive ObjectA.ObjectId
  3. Compare: ObjectA.origin != local_origin ✗ (different machine!)
  4. Use "tcp://192.168.1.10:8080" instead
  5. Make RPC calls (network)
```

### Implementation Status

✅ SharedMemoryChannel (Boost.Interprocess message_queue)  
✅ SharedMemoryConnection (RPC session wrapper)  
✅ EndPoint parsing (`mem://channel_id`)  
⚠️ POA integration (add mem:// URLs to ObjectId) - **TODO**  
⚠️ Origin comparison logic in client - **TODO**  
⚠️ Automatic queue cleanup on object destruction - **TODO**  

### Next Steps

1. Remove SharedMemoryListener (not needed)
2. Integrate with POA: add mem:// URL when creating objects
3. Add origin comparison in get_session()
4. Add queue cleanup when objects are destroyed
5. Consider system queue limits (may need managed_shared_memory for high object count)
