# HTTP Authentication & Cookies

NPRPC supports cookie-based authentication for **HTTP** and **WebSocket** sessions.  
Native transports (TCP, shared-memory, UDP) have no HTTP header layer; credentials for those should be passed as ordinary IDL arguments.

---

## Why cookies and not headers?

| Approach | Pro | Con |
|---|---|---|
| Auth token as IDL argument | Works on all transports | Token visible to JS; must be managed manually |
| Custom HTTP header (`Authorization`) | Explicit, easy to trace | Requires `credentials: 'include'` + CORS change anyway; not `httpOnly` |
| **`httpOnly` cookie (this feature)** | **Never accessible to JS; browser sends automatically; standard pattern** | HTTP/WebSocket only |

`httpOnly` cookies are set and read entirely by the server. JavaScript cannot access them via `document.cookie`, which eliminates XSS-based token theft.

---

## How It Works

### Request path (browser → server)

1. The browser makes an RPC call via `fetch()` with `credentials: 'include'`.  
   This is emitted automatically by the npidl code generator for all HTTP proxy methods.
2. The browser attaches all matching cookies in the `Cookie:` request header.
3. The server extracts the header and stores it in `SessionContext::cookies` before dispatching the call.
4. Inside the servant method, `nprpc::http::get_cookie("name")` reads from that field.

### Response path (server → browser)

1. The servant calls `nprpc::http::set_cookie("name", value, opts)`, which appends to `SessionContext::set_cookies`.
2. After dispatch returns, the HTTP handler reads `set_cookies` and adds a `Set-Cookie:` header to the response for each entry.
3. The browser stores the cookie and sends it on every subsequent request to the same origin.

### WebSocket sessions

Cookies from the HTTP `Upgrade` request are captured once at handshake time and stored in `SessionContext::cookies` for the lifetime of the connection. `get_cookie()` works identically inside any servant dispatched over that WebSocket. `set_cookie()` queues entries in `set_cookies`, but there is no per-call response envelope on WebSocket — to issue a new token mid-session, make a short HTTP RPC call for the login step instead.

### CORS

When a request carries an `Origin` header (any cross-origin browser call), the server:
- Echoes the exact `Origin` value back in `Access-Control-Allow-Origin` (wildcards are rejected by browsers for credentialed requests).
- Adds `Access-Control-Allow-Credentials: true`.

This is handled automatically by `handle_rpc_request` in `src/http/http_server.cpp` and requires no configuration.

---

## C++ API

Include the header:

```cpp
#include <nprpc/http_auth.hpp>
```

### `get_cookie`

```cpp
std::optional<std::string> nprpc::http::get_cookie(std::string_view name);
```

Returns the value of a named cookie from the current request, or `std::nullopt` when:
- the cookie is absent, or
- called outside a servant dispatch, or
- the session is a native transport (TCP / SHM / UDP).

### `set_cookie`

```cpp
void nprpc::http::set_cookie(
    std::string_view name,
    std::string_view value,
    const CookieOptions& opts = {});
```

Queues a `Set-Cookie` header for the current HTTP response.  
`CookieOptions` fields (all have sensible defaults):

| Field | Type | Default | Description |
|---|---|---|---|
| `http_only` | `bool` | `true` | Blocks JS access via `document.cookie` |
| `secure` | `bool` | `true` | HTTPS-only transmission |
| `same_site` | `std::string_view` | `"Strict"` | `"Strict"`, `"Lax"`, or `"None"` |
| `max_age` | `std::optional<int>` | `std::nullopt` | Lifetime in seconds; omit for session cookie |
| `path` | `std::string_view` | `"/"` | URL path scope |
| `domain` | `std::string_view` | `""` | Domain scope; empty = current host only |

### `clear_cookie`

```cpp
void nprpc::http::clear_cookie(
    std::string_view name,
    std::string_view path   = "/",
    std::string_view domain = {});
```

Removes a cookie by setting `Max-Age=0`.

### Full example

```cpp
// Login servant method
void MyAuthServant::Login(
    flat_buffer& buf,
    const std::string& username,
    const std::string& password)
{
    auto token = authenticate(username, password); // your logic

    nprpc::http::set_cookie("session_id", token, {
        .http_only = true,
        .secure    = true,
        .same_site = "Strict",
        .max_age   = 86400,   // 1 day
    });
}

// Protected servant method
void MyDataServant::GetData(flat_buffer& buf)
{
    auto token = nprpc::http::get_cookie("session_id");
    if (!token)
        throw nprpc::Exception("Unauthenticated");

    auto user = validate_token(*token); // your logic
    // ... serve data ...
}

// Logout servant method
void MyAuthServant::Logout(flat_buffer& buf)
{
    nprpc::http::clear_cookie("session_id");
}
```

---

## TypeScript / JavaScript

No changes are needed in calling code. The npidl generator adds `credentials: 'include'` to every `fetch()` call in HTTP proxy stubs:

```ts
const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream' },
    credentials: 'include',   // ← emitted automatically
    body: buf.array_buffer
});
```

The browser attaches cookies automatically on every call to the same origin and stores any `Set-Cookie` headers returned by the server.

---

## Swift API

```swift
import NPRPC
```

### `getCookie(name:)`

```swift
func getCookie(name: String) -> String?
```

Returns the cookie value, or `nil` when absent or outside an HTTP/WS dispatch.

### `setCookie(name:value:options:)`

```swift
func setCookie(name: String, value: String, options: CookieOptions = CookieOptions())
```

`CookieOptions` mirrors the C++ struct:

```swift
public struct CookieOptions {
    public var httpOnly: Bool   = true
    public var secure:   Bool   = true
    public var sameSite: String = "Strict"
    public var maxAge:   Int?   = nil      // nil = session cookie
    public var path:     String = "/"
    public var domain:   String = ""
}
```

### `clearCookie(name:path:domain:)`

```swift
func clearCookie(name: String, path: String = "/", domain: String = "")
```

### Full example

```swift
// Inside a Swift servant's dispatch() override (or generated method):

// Login
let token = authenticateUser(username, password)
setCookie(name: "session_id", value: token, options: CookieOptions(
    httpOnly: true,
    secure:   true,
    sameSite: "Strict",
    maxAge:   86400
))

// Protected call
guard let token = getCookie(name: "session_id") else {
    throw NPRPCException.unauthenticated
}
let user = validateToken(token)

// Logout
clearCookie(name: "session_id")
```

---

## Transport Support Matrix

| Transport | `get_cookie` | `set_cookie` | Notes |
|---|---|---|---|
| HTTP | ✅ | ✅ | Full support; `Set-Cookie` sent with each RPC reply |
| WebSocket | ✅ | ⚠️ | Read from Upgrade request; write is no-op (no response envelope) |
| TCP | — | — | Returns `nullopt` / no-op; pass credentials as IDL arguments |
| Shared Memory | — | — | Same as TCP |
| UDP | — | — | Same as TCP |
| QUIC / HTTP/3 | ✅ | ✅ | Same as HTTP; handled by `http3_server_msh3.cpp` / `http3_server_nghttp3.cpp` |

---

## Security Recommendations

- Always use `httpOnly: true` for session tokens — it prevents XSS-based theft.
- Always use `secure: true` in production — cookies are sent only over HTTPS.
- Use `sameSite: "Strict"` unless you need cross-site embedding (then use `"Lax"`).  
  `"None"` requires `secure: true` and explicitly opts-in to cross-site sending.
- Rotate session tokens after privilege escalation (e.g., after login, issue a fresh token and call `set_cookie` again with the new value).
- Use short `max_age` values and refresh tokens server-side instead of long-lived permanent cookies.
- Do **not** log cookie values — treat them like passwords.
