# Server-Rendered Pages

A page handler renders HTML in the process that owns the service
implementations, so a page can be built from data the server already holds
instead of shipping a shell to the browser and waiting for it to call back.

```
browser ──HTTP/1.1, HTTP/3──▶ NPRPC HTTP server
                                 ├── /rpc            → RPC dispatch
                                 ├── page handler    → your HTML
                                 └── everything else → static file cache (zero-copy)
```

It is available wherever `NPRPC_ENABLE_HTTP` or `NPRPC_ENABLE_HTTP3` is on.

## C++

```cpp
#include <nprpc/page_handler.hpp>

builder.with_page_handler([&](const nprpc::PageRequest& req)
                              -> std::optional<nprpc::PageResponse> {
  if (req.path != "/blog") return std::nullopt;  // decline -> normal routing
  nprpc::PageResponse res;
  res.body = render_blog(repository.list_posts(page_of(req), 5));
  return res;
});
```

`PageRequest` carries `method`, `target`, the already-split `path` and `query`,
lowercased `headers`, `body`, and `client_address`. `PageResponse` carries
`status` (default 200), `headers`, and `body`.

## Swift

```swift
builder.withPageHandler { request in
    guard request.path == "/blog" else { return nil }
    let page = Int(request.queryItems["page"] ?? "1") ?? 1
    return PageResponse(html: renderBlog(page))
}
```

`PageRequest.queryItems` parses the query string, percent-decoding and treating
`+` as a space. `PageResponse` has an `html:` initializer and a `redirect(to:)`
helper that defaults to 303, the right status after a POST.

For templates, the `NPRPCWeb` library (a separate product of the `nprpc_swift`
package) provides `TemplateLibrary`: it loads a directory of Mustache
templates and can re-read them on every request during development.

```swift
import NPRPCWeb

let templates = try TemplateLibrary(directory: "templates", hotReload: true)
builder.withPageHandler { request in
    guard request.path == "/" else { return nil }
    return templates.render(HomeView(), withTemplate: "home")
        .map { PageResponse(html: $0) }
}
```

Two complete examples:

- `examples/live-blog`: Mustache pages rendered from npidl-generated structs,
  htmx for fragment swaps, and browser islands for the parts that need a live
  RPC connection.
- `docs/site`: this documentation site.

## Routing

The handler sees every GET, HEAD and POST except framework paths — `/rpc` and
`/_nprpc/...` are excluded before it runs, so a catch-all route cannot shadow
them by accident.

Returning "no response" (`std::nullopt` in C++, `nil` in Swift) declines the
request and the server carries on to the zero-copy static file cache. Declining
unknown paths is how assets keep their fast path, so a handler only needs to
know its own routes.

## Threading

The handler is called synchronously on an HTTP I/O thread, and on several of
them concurrently when the server runs a thread pool, so it must be thread-safe.
That suits a template render, which takes microseconds. Anything slow — a
database round-trip, an outbound network call — blocks an I/O thread and belongs
on another thread, with the handler serving what it already has.

An unhandled C++ exception from the handler is caught and logged, and the
request falls through as if the handler had declined. A Swift handler that traps
takes the process down, as any trap does.

## Response headers

A handler's headers are sent on both HTTP/1.1 and HTTP/3. `content-type`
defaults to `text/html; charset=utf-8`. `content-length` and
`transfer-encoding` are ignored, because the server sets them itself.

## Related

- [Build options](./BUILD.md)
- [Cookie auth](./HTTP_AUTH.md)
- [npquicrouter](../npquicrouter/README.md), for running several HTTP/3 sites on one address
