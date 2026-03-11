# Live Blog Swift Server

This directory contains the Swift backend for the `examples/live-blog` demo.

The Svelte client is shared with the C++ backend under `examples/live-blog/cpp`, so both server implementations expose the same browser-facing contracts.

## Current State

- standalone Swift package using the preinstalled `nprpc_swift` package from the dev image
- generated `LiveBlogAPI` target compiled alongside the executable target
- mock repository-backed `BlogService`, plus toy `ChatService` and `MediaService`
- `host.json` generation for browser bootstrap

## Intended Runtime Model

- Swift owns business logic, mock data, and later persistence
- SvelteKit renders route-aware shells only
- browser hydration makes the real NPRPC calls to this Swift server
- live chat uses bidi streams
- video/media can later use server streams from the same runtime

## Running In The Development Image

From `examples/live-blog`:

```bash
./scripts/build-swift-server.sh --debug
```

Or from the repo root through CMake:

```bash
cmake -S . -B build \
  -DNPRPC_BUILD_EXAMPLES=ON \
  -DNPRPC_BUILD_TOOLS=ON
cmake --build build --target live_blog_example
```

The CMake target depends on the developer Docker image, rebuilds the Vite client, and then runs the same Swift build flow inside `nprpc-dev:latest`.

To run the server from CMake after building:

```bash
cmake --build build --target run_live_blog_swift_server
```

Then run the binary in the development image:

```bash
docker run --rm -it \
  -v $(pwd):/project \
  -w /project/examples/live-blog/server \
  -p 8443:8443 \
  nprpc-dev:latest \
  ./.build/debug/LiveBlogServer
```

## Regenerating Stubs

```bash
python3 ../scripts/gen_stubs.py
```

The IDL lives in [examples/live-blog/idl/live_blog.npidl](../idl/live_blog.npidl). Generated Swift output lands in `Sources/LiveBlogAPI/`, and generated TypeScript output lands in `../client/src/rpc/`.