# nprpc_node

Native Node.js addon for NPRPC shared memory transport. Enables zero-copy IPC between Node.js and C++ NPRPC servers using memory-mapped ring buffers.

## Features

- **Zero-copy IPC**: Uses memory-mapped shared memory for minimal latency
- **Lock-free ring buffers**: SPSC (Single Producer, Single Consumer) design
- **High throughput**: ~1+ GB/s for large messages
- **TypeScript support**: Full type definitions included
- **Cross-process**: Works between Node.js and C++ NPRPC servers

## Installation

```bash
npm install
# or
npm run rebuild
```

### Prerequisites

- Node.js >= 18.0.0
- C++23 compiler (GCC 13+ or Clang 16+)
- Boost libraries (boost_thread, boost_interprocess)
- OpenSSL development headers

On Ubuntu/Debian:
```bash
sudo apt install libboost-dev libboost-thread-dev libssl-dev
```

On Arch Linux:
```bash
sudo pacman -S boost openssl
```

## Usage

```javascript
const { ShmChannel } = require('nprpc_node');

// Server side (creates shared memory)
const server = new ShmChannel('my_channel', { isServer: true, create: true });

// Client side (connects to existing shared memory)
const client = new ShmChannel('my_channel', { isServer: false, create: false });

// Send data (server -> client uses s2c ring buffer)
server.send(new Uint8Array([1, 2, 3, 4]));

// Receive data on client
if (client.hasData()) {
    const data = client.tryReceive();
    console.log('Received:', data);
}

// Bidirectional: client -> server uses c2s ring buffer
client.send(new Uint8Array([5, 6, 7, 8]));
const serverData = server.tryReceive();

// Clean up
client.close();
server.close();
```

## API

### `new ShmChannel(channelId, options)`

Create a new shared memory channel.

- `channelId`: Unique string identifier (shared between server and client)
- `options`:
  - `isServer`: `true` for server side, `false` for client
  - `create`: `true` to create ring buffers (server), `false` to open existing (client)

### `isOpen(): boolean`

Check if the channel is open and ready.

### `getChannelId(): string | null`

Get the channel identifier.

### `send(data: Uint8Array): boolean`

Send data through the channel. Returns `true` if sent, `false` if buffer full.

### `tryReceive(): Uint8Array | null`

Non-blocking receive. Returns data or `null` if no data available.

### `hasData(): boolean`

Check if data is available to read.

### `close(): void`

Close the channel and release resources.

## Architecture

The addon uses two ring buffers per channel:
- `s2c` (server-to-client): Server writes, client reads
- `c2s` (client-to-server): Client writes, server reads

Ring buffer configuration:
- Buffer size: 16 MB per direction (32 MB total)
- Max message size: 32 MB
- Memory layout: Mirrored mapping for wrap-around-free access

Shared memory naming convention:
- `/nprpc_{channelId}_s2c`
- `/nprpc_{channelId}_c2s`

## SvelteKit SSR Integration

This addon is designed to enable NPRPC communication from SvelteKit server-side rendering:

```javascript
// In a SvelteKit server route (+page.server.ts)
import { ShmChannel } from 'nprpc_node';

let channel: ShmChannel | null = null;

export async function load() {
    if (!channel) {
        channel = new ShmChannel('svelte_ssr', { isServer: false, create: false });
    }
    
    // Send RPC request
    channel.send(serializeRequest({ method: 'getData' }));
    
    // Wait for response (with timeout)
    const response = await waitForResponse(channel, 5000);
    
    return deserializeResponse(response);
}
```

## License

MIT
