# Live Blog Swift Server

This directory contains the Swift backend for the `examples/live-blog` demo.

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