// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// In-process server-side rendering.
//
// A page handler renders HTML in the same process that owns the service
// implementations, so a page can be built from data the server already has
// instead of shipping a shell to the browser and waiting for it to call back.

import CNprpc
import Foundation

/// One HTTP request offered to a `PageHandler`.
public struct PageRequest: Sendable {
    /// "GET", "HEAD" or "POST".
    public let method: String
    /// Path and query together, e.g. `/blog?page=2`.
    public let target: String
    /// Path only, e.g. `/blog`.
    public let path: String
    /// Query string without the leading `?`, empty when there is none.
    public let query: String
    /// Request headers, keyed by lowercase field name.
    public let headers: [String: String]
    /// Request body; empty for GET and HEAD.
    public let body: [UInt8]
    /// Peer address, empty when unavailable.
    public let clientAddress: String

    /// Construct a request directly.  The server builds these itself; this is
    /// for testing a handler without standing a server up.
    public init(method: String,
                target: String,
                path: String,
                query: String = "",
                headers: [String: String] = [:],
                body: [UInt8] = [],
                clientAddress: String = "") {
        self.method = method
        self.target = target
        self.path = path
        self.query = query
        self.headers = headers
        self.body = body
        self.clientAddress = clientAddress
    }

    /// Query parameters parsed from `query`, percent-decoding `+` as a space.
    ///
    /// A repeated key keeps its first value.  Returns an empty dictionary when
    /// there is no query string.
    public var queryItems: [String: String] {
        guard !query.isEmpty else { return [:] }
        var items: [String: String] = [:]
        for pair in query.split(separator: "&", omittingEmptySubsequences: true) {
            let parts = pair.split(separator: "=", maxSplits: 1,
                                   omittingEmptySubsequences: false)
            guard let rawName = parts.first else { continue }
            let name = Self.decode(String(rawName))
            guard !name.isEmpty, items[name] == nil else { continue }
            items[name] = parts.count > 1 ? Self.decode(String(parts[1])) : ""
        }
        return items
    }

    private static func decode(_ s: String) -> String {
        s.replacingOccurrences(of: "+", with: " ").removingPercentEncoding
            ?? s.replacingOccurrences(of: "+", with: " ")
    }
}

/// A handler's answer for a `PageRequest`.
public struct PageResponse: Sendable {
    public var status: Int
    /// Response headers.  `content-type` defaults to `text/html; charset=utf-8`
    /// when left unset.
    public var headers: [String: String]
    public var body: [UInt8]

    public init(status: Int = 200,
                headers: [String: String] = [:],
                body: [UInt8]) {
        self.status = status
        self.headers = headers
        self.body = body
    }

    /// An HTML response.
    public init(html: String,
                status: Int = 200,
                headers: [String: String] = [:]) {
        self.init(status: status, headers: headers, body: Array(html.utf8))
    }

    /// A redirect, defaulting to 303 See Other — the right status after a POST,
    /// since it makes the browser follow up with a GET.
    public static func redirect(to location: String,
                                status: Int = 303) -> PageResponse {
        PageResponse(status: status,
                     headers: ["location": location],
                     body: [])
    }
}

/// Renders a page, or returns `nil` to decline the request.
///
/// Declining falls through to the static file cache, so returning `nil` for
/// paths you do not recognise is how assets keep their zero-copy path.
///
/// Called synchronously on an HTTP I/O thread, and on several concurrently when
/// the server runs a thread pool, hence `@Sendable`.  A render that blocks holds
/// up an I/O thread, so anything slow (a database round-trip, a network call)
/// belongs elsewhere, with the handler serving what it already has.
public typealias PageHandler = @Sendable (PageRequest) -> PageResponse?

// MARK: - C bridge

/// Owns a `PageHandler` for the lifetime of the process and bridges the C ABI.
///
/// The box is deliberately never released: the C++ server holds the raw pointer
/// inside its global config and calls it until shutdown, so there is no moment
/// where freeing it would be safe and nothing to gain by trying.
final class PageHandlerBox: @unchecked Sendable {
    let handler: PageHandler

    init(_ handler: @escaping PageHandler) {
        self.handler = handler
    }
}

/// Trampoline handed to C++.  Rebuilds a Swift `PageRequest`, runs the handler,
/// and writes any response back through the opaque setters.
let nprpcPageHandlerTrampoline: @convention(c) (
    UnsafeMutableRawPointer?,
    UnsafePointer<nprpc_page_request>?,
    UnsafeMutableRawPointer?
) -> Bool = { ctx, request, response in
    guard let ctx, let request, let response else { return false }

    let box = Unmanaged<PageHandlerBox>.fromOpaque(ctx).takeUnretainedValue()
    let req = request.pointee

    var headers: [String: String] = [:]
    if let names = req.header_names, let values = req.header_values {
        headers.reserveCapacity(req.header_count)
        for i in 0..<req.header_count {
            guard let name = names[i], let value = values[i] else { continue }
            headers[String(cString: name)] = String(cString: value)
        }
    }

    var body: [UInt8] = []
    if let data = req.body, req.body_len > 0 {
        body = Array(UnsafeRawBufferPointer(start: data, count: req.body_len))
    }

    let swiftRequest = PageRequest(
        method: req.method.map { String(cString: $0) } ?? "",
        target: req.target.map { String(cString: $0) } ?? "",
        path: req.path.map { String(cString: $0) } ?? "",
        query: req.query.map { String(cString: $0) } ?? "",
        headers: headers,
        body: body,
        clientAddress: req.client_address.map { String(cString: $0) } ?? ""
    )

    guard let result = box.handler(swiftRequest) else { return false }

    nprpc_page_response_set_status(response, UInt32(result.status))
    for (name, value) in result.headers {
        nprpc_page_response_set_header(response, name, value)
    }
    if result.body.isEmpty {
        nprpc_page_response_set_body(response, nil, 0)
    } else {
        result.body.withUnsafeBufferPointer { buf in
            buf.baseAddress!.withMemoryRebound(to: CChar.self,
                                               capacity: buf.count) { p in
                nprpc_page_response_set_body(response, p, buf.count)
            }
        }
    }
    return true
}
