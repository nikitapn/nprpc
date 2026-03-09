---
title: Live Blog Example
---

# Live Blog Example

`examples/live-blog` is a hybrid NPRPC demo workspace built around a simple blog product:

- blog listing and pagination via regular typed RPC calls
- post pages with comments via regular typed RPC calls
- post-specific live chat via bidi streams
- room for later media/video streaming from the same Swift backend

The goal is to demonstrate a realistic architecture rather than a toy transport benchmark.

## Architecture

This example intentionally uses a hybrid split:

- `client/`: SvelteKit + Svelte 5 + Tailwind for route-aware SSR shell generation
- `server/`: Swift app that owns all business logic, mock data or databases, and NPRPC services

The SSR layer does not run business logic. It renders the route-aware shell only.

Example:

- user visits `/blog?page=10`
- SSR renders the page frame, heading, shell, and loading placeholders for page 10
- browser hydration performs the real NPRPC call to the Swift backend to load page 10 posts

This keeps domain logic in Swift and avoids needing Node-side RPC support.

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
└── server/   # Swift NPRPC backend scaffold
```

## Client Notes

The client is a SvelteKit app with Svelte 5 and Tailwind.

It now does two important things:

- keeps SSR limited to route-aware shell rendering
- hydrates blog list, post detail, comments, and author pages from the Swift backend via generated NPRPC TypeScript stubs

For development, the Vite server proxies `/host.json` to the Swift backend on `https://localhost:8443`, while the generated object URLs continue to point the browser at the real NPRPC endpoint.

## Server Notes

The Swift server now includes:

- a standalone Swift package under `server/`
- a generated `LiveBlogAPI` target for RPC contracts
- a handwritten `LiveBlogServer` executable with mock repository-backed services
- `host.json` emission for browser bootstrap

The server is intended to run inside the updated development Docker image.

## Development Workflow

### Client

```bash
cd examples/live-blog/client
npm install
npm run dev
```

### Server

From `examples/live-blog`:

```bash
./scripts/build-swift-server.sh --debug
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
2. Serve the built client from the Swift static root for a single-process demo
3. Add live chat on the post page
4. Add media streaming after the core blog flow is solid