// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// HTTP cookie helpers for NPRPC servants dispatched over HTTP or WebSocket.
//
// These are only meaningful inside a servant's dispatch() method (where
// the C++ session context is set on the calling thread).  They are safe
// but silently return nil / do nothing for native transports (TCP/SHM/UDP).
//
// Usage inside a Swift servant:
//
//   // Read an incoming cookie:
//   if let token = getCookie(name: "session_id") { ... }
//
//   // Write a server-set httpOnly cookie (HTTP transport only):
//   setCookie(name: "session_id", value: token)
//
//   // Remove a cookie:
//   clearCookie(name: "session_id")

import CNprpc

/// Options controlling a `Set-Cookie` response header.
public struct CookieOptions {
    /// Prevent JavaScript access (recommended for session tokens).
    public var httpOnly: Bool = true
    /// Send only over HTTPS.
    public var secure: Bool = true
    /// SameSite attribute — `"Strict"`, `"Lax"`, or `"None"`.
    /// Use `"None"` together with `secure = true` for cross-site cookies.
    public var sameSite: String = "Strict"
    /// Lifetime in seconds.  `nil` = session cookie (no `Max-Age` header).
    public var maxAge: Int? = nil
    /// Path scope.  Defaults to `"/"`.
    public var path: String = "/"
    /// Domain scope.  Empty string = current host only (the default).
    public var domain: String = ""

    public init(
        httpOnly: Bool = true,
        secure: Bool = true,
        sameSite: String = "Strict",
        maxAge: Int? = nil,
        path: String = "/",
        domain: String = ""
    ) {
        self.httpOnly = httpOnly
        self.secure = secure
        self.sameSite = sameSite
        self.maxAge = maxAge
        self.path = path
        self.domain = domain
    }
}

// ─── Cookie helpers ──────────────────────────────────────────────────────────

/// Read a named cookie from the current HTTP or WebSocket request.
///
/// Returns `nil` when:
/// - called outside a servant `dispatch()`, or
/// - the named cookie is absent, or
/// - the transport is native (TCP / shared-memory / UDP).
public func getCookie(name: String) -> String? {
    guard let ptr = nprpc_http_get_cookie(name) else { return nil }
    return String(cString: ptr)
}

/// Queue a `Set-Cookie` header to be included in the HTTP response.
///
/// - For HTTP sessions the header is sent with the RPC reply.
/// - For WebSocket sessions the cookie is queued but there is no per-call
///   response envelope; use an HTTP RPC call to issue new tokens instead.
/// - No-op for native transports (TCP / SHM / UDP).
public func setCookie(
    name: String,
    value: String,
    options: CookieOptions = CookieOptions()
) {
    nprpc_http_set_cookie(
        name,
        value,
        options.httpOnly,
        options.secure,
        options.sameSite,
        Int32(options.maxAge ?? -1),
        options.path,
        options.domain
    )
}

/// Remove a cookie on the client side by sending `Max-Age=0`.
public func clearCookie(
    name: String,
    path: String = "/",
    domain: String = ""
) {
    nprpc_http_clear_cookie(name, path, domain)
}
