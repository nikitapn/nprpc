---
title: Live Blog Example
---

# Live Blog Example

`examples/live-blog` is the unified NPRPC demo workspace built around a simple blog product:

- blog listing and pagination via regular typed RPC calls
- post pages with comments via regular typed RPC calls
- post-specific live chat via bidi streams
- room for later media/video streaming from the same backend contract

The goal is to demonstrate a realistic architecture rather than a toy transport benchmark.

## Architecture

This example intentionally uses a hybrid split:

- `client/`: SvelteKit + Svelte 5 + Tailwind for route-aware SSR shell generation
- `server/`: Swift app that owns one implementation of the demo services
- `cpp/`: C++ app that serves the same Svelte build and implements the same demo contracts

The SSR layer does not run business logic. It renders the route-aware shell only.

Example:

- user visits `/blog?page=10`
- SSR renders the page frame, heading, shell, and loading placeholders for page 10
- browser hydration performs the real NPRPC call to the active backend to load page 10 posts

This keeps domain logic on the NPRPC backend and avoids needing Node-side RPC support.

## Planned Feature Coverage

### Blog

- `listPosts(page, pageSize)`
- `getPost(slug)`
- `listComments(postId, page)`
- `getAuthor(slug)`

### Chat

- `joinPostChat(postId)` using a bidi stream
- client sends messages upstream
- server pushes chat messages, join/leave events, and moderation events downstream

### Media Later

- server stream for post-attached video or audio
- same Swift server, same typed contracts, different interaction mode

## Folder Layout

```text
examples/live-blog/
├── README.md
├── client/   # SvelteKit shell + hydration app
├── server/   # Swift NPRPC backend scaffold
└── cpp/      # C++ NPRPC backend using the same client build
```

## Client Notes

The client is a SvelteKit app with Svelte 5 and Tailwind.

It now does two important things:

- keeps SSR limited to route-aware shell rendering
- hydrates blog list, post detail, comments, and author pages from the active backend via generated NPRPC TypeScript stubs

For development, the Vite server proxies `/host.json` to the Swift backend on `https://localhost:8443`, while the generated object URLs continue to point the browser at the real NPRPC endpoint. The production build can be served by either the Swift or C++ server variant.

## Server Notes

The server side now includes:

- a standalone Swift package under `server/`
- a native C++ server under `cpp/`
- a generated `LiveBlogAPI` target for RPC contracts
- handwritten server implementations with mock repository-backed services
- `host.json` emission for browser bootstrap

The server is intended to run inside the updated development Docker image.

## Development Workflow

The live-blog client is an npm workspace member of the repo root. Install JavaScript dependencies once from the repo root before building or running the client:

```bash
npm ci
```

### Client

```bash
npm run dev --workspace live-blog-client
```

### Swift Server

From `examples/live-blog`:

```bash
./scripts/build-swift-server.sh --debug
```

Or build it from the repo root with CMake:

```bash
npm ci
cmake -S . -B build \
  -DNPRPC_BUILD_EXAMPLES=ON \
  -DNPRPC_BUILD_TOOLS=ON
cmake --build build --target live_blog_example
```

That target generates stubs, builds the `nprpc-dev:latest` image, runs the Vite client build, builds the C++ backend, and then runs `swift build` inside the container.

### C++ Server

Build and run the C++ backend from the repo root:

```bash
cmake -S . -B build \
  -DNPRPC_BUILD_EXAMPLES=ON \
  -DNPRPC_BUILD_TOOLS=ON
cmake --build build --target run_live_blog_cpp_server
```

The C++ server serves the same built Svelte app and publishes the `blog` and `chat` objects used by the current routes in `host.json`.

To run the built Swift server from CMake:

```bash
cmake --build build --target run_live_blog_swift_server
```

To run the server in the dev image after building:

```bash
docker run --rm -it \
  -v $(pwd):/../..:/project \
  -w /project/examples/live-blog/server \
  -p 8443:8443 \
  nprpc-dev:latest \
  ./.build/debug/LiveBlogServer
```

## Why This Example

This shape demonstrates the framework in layers:

1. ordinary app requests and pagination
2. typed real-time chat through bidi streams
3. later, media/video streaming from the same server runtime

That makes the transport and streaming story obvious without reducing the product to a raw framework demo.

## Next Steps

1. Replace the toy chat loopback with multi-user room state
2. Keep the Swift and C++ mock repositories aligned so both backends remain interchangeable
3. Add live chat on the post page
4. Add media streaming after the core blog flow is solid