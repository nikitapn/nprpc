# Live Blog Swift Server

This directory contains the Swift backend scaffold for the `examples/live-blog` demo.

## Current State

- standalone Swift package using the repo-local `nprpc_swift` package
- NPRPC HTTP/HTTPS server scaffold in `Sources/LiveBlogServer/main.swift`
- initial service contract sketch in `idl/live_blog.npidl`
- helper generation script in `scripts/gen_stubs.sh`

## Intended Runtime Model

- Swift owns business logic, mock data, and later persistence
- SvelteKit renders route-aware shells only
- browser hydration makes the real NPRPC calls to this Swift server
- live chat uses bidi streams
- video/media can later use server streams from the same runtime

## Running In The Development Image

From the repo root:

```bash
docker run --rm -it \
  -v $(pwd):/project \
  -w /project/examples/live-blog/server \
  -p 8443:8443 \
  nprpc-dev:latest \
  swift run
```

## Generating Stubs Later

```bash
docker run --rm -it \
  -v $(pwd):/project \
  -w /project/examples/live-blog/server \
  nprpc-dev:latest \
  bash scripts/gen_stubs.sh
```

The current scaffold does not yet compile generated example-specific Swift code into the server target. That should be the next step after the service contract is finalized.