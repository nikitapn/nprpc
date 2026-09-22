# HTTP Authentication & Cookies

Servants called from a browser can read and set cookies. An `httpOnly` session
cookie is the usual way to authenticate browser clients: the browser sends it
with every call, and page scripts cannot read it, so an XSS bug cannot steal
it.

Cookies exist on HTTP, HTTP/3 and WebSocket. Native transports (TCP, shared
memory, QUIC) have no headers, so pass credentials there as ordinary IDL
arguments.

## How it works

1. The browser calls a method. Generated TypeScript proxies send every HTTP
   call with `credentials: 'include'`, so the browser attaches its cookies.
2. Inside the servant, `get_cookie("name")` reads them.
3. `set_cookie(...)` queues a `Set-Cookie` header, which the server adds to the
   reply once the method returns.

On a WebSocket, the cookies are read once from the connection's upgrade
request and stay the same for the whole connection. There is no reply envelope
to carry a `Set-Cookie`, so `set_cookie` has no effect there. Make the login
call over HTTP instead.

| Transport | Read cookies | Set cookies |
|---|---|---|
| HTTP, HTTP/3 | yes | yes |
| WebSocket | yes (from the upgrade request) | no |
| TCP, shared memory, QUIC | no (`nullopt` / `nil`) | no (ignored) |

## Cross-origin calls

A page served by the same server needs no configuration. A page on another
origin, for example `https://app.example.com` calling `https://api.example.com`,
must be listed:

```cpp
builder.with_http(443)
    .ssl("cert.pem", "key.pem")
    .allow_origins({"https://app.example.com"});
```

For a listed origin the server replies with that exact origin in
`Access-Control-Allow-Origin` and sets `Access-Control-Allow-Credentials: true`,
which browsers require before they send cookies cross-origin. Calls from
unlisted origins get no CORS headers, so the browser blocks them.

## C++

```cpp
#include <nprpc/http_auth.hpp>

std::optional<std::string> nprpc::http::get_cookie(std::string_view name);

void nprpc::http::set_cookie(std::string_view name,
                             std::string_view value,
                             const CookieOptions& opts = {});

void nprpc::http::clear_cookie(std::string_view name,
                               std::string_view path = "/",
                               std::string_view domain = {});
```

`get_cookie` returns `std::nullopt` when the cookie is absent, outside a
servant method, or on a transport without cookies. `clear_cookie` expires the
cookie by setting `Max-Age=0`.

`CookieOptions` defaults to the safe choices:

| Field | Default | Meaning |
|---|---|---|
| `http_only` | `true` | Hidden from page scripts |
| `secure` | `true` | Sent over HTTPS only |
| `same_site` | `"Strict"` | `"Strict"`, `"Lax"` or `"None"` |
| `max_age` | `std::nullopt` | Lifetime in seconds; unset = until the browser closes |
| `path` | `"/"` | URL path scope |
| `domain` | `""` | Empty = this host only |

Given this IDL:

```npidl
exception Unauthenticated {}

interface Auth {
  void Login(username: string, password: string) raises(Unauthenticated);
  void Logout();
}
interface Data {
  string GetProfile() raises(Unauthenticated);
}
```

the servants look like this:

```cpp
class AuthImpl : public IAuth_Servant {
public:
  void Login(nprpc::flat::Span<char> username,
             nprpc::flat::Span<char> password) override
  {
    auto token = authenticate(username, password);   // your logic
    if (!token)
      throw Unauthenticated();
    nprpc::http::set_cookie("session_id", *token, {.max_age = 86400});
  }

  void Logout() override { nprpc::http::clear_cookie("session_id"); }
};

class DataImpl : public IData_Servant {
public:
  std::string GetProfile() override
  {
    auto token = nprpc::http::get_cookie("session_id");
    if (!token)
      throw Unauthenticated();
    return load_profile(*token);                      // your logic
  }
};
```

Because the methods declare `raises(Unauthenticated)`, the client receives the
error as that exception type in every language.

## Swift

```swift
import NPRPC

func getCookie(name: String) -> String?
func setCookie(name: String, value: String, options: CookieOptions = CookieOptions())
func clearCookie(name: String, path: String = "/", domain: String = "")
```

`CookieOptions` has the same fields and defaults as in C++: `httpOnly`,
`secure`, `sameSite`, `maxAge`, `path` and `domain`.

```swift
final class AuthImpl: AuthServant, @unchecked Sendable {
    override func login(username: String, password: String) throws {
        guard let token = authenticate(username, password) else {  // your logic
            throw Unauthenticated()
        }
        setCookie(name: "session_id", value: token,
                  options: CookieOptions(maxAge: 86400))
    }

    override func logout() {
        clearCookie(name: "session_id")
    }
}

final class DataImpl: DataServant, @unchecked Sendable {
    override func getProfile() throws -> String {
        guard let token = getCookie(name: "session_id") else {
            throw Unauthenticated()
        }
        return loadProfile(token)                          // your logic
    }
}
```

## TypeScript

Nothing to do: generated proxies already send `credentials: 'include'`, and
the browser stores `Set-Cookie` replies itself.

## Security checklist

- Keep `http_only` and `secure` on for session cookies.
- Prefer `same_site = "Strict"`; use `"Lax"` only if you need top-level
  cross-site navigation to carry the cookie. `"None"` requires `secure`.
- Issue a new token after login or any privilege change.
- Keep `max_age` short and refresh server-side, rather than using
  long-lived cookies.
- Never log cookie values.
