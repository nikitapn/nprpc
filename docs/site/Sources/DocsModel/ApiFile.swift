// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation

// The api.json written by npdoc (format 1). See npdoc/README.md.

public struct ApiFile: Decodable, Sendable {
    public var format: Int
    public var symbols: [SymbolRecord]
    public var guides: [GuideRecord]

    public init(format: Int = 1, symbols: [SymbolRecord], guides: [GuideRecord] = []) {
        self.format = format
        self.symbols = symbols
        self.guides = guides
    }

    public static func load(from path: String) throws -> ApiFile {
        let data = try Data(contentsOf: URL(fileURLWithPath: path))
        let file = try JSONDecoder().decode(ApiFile.self, from: data)
        guard file.format == 1 else { throw DocsError.unsupportedFormat(file.format) }
        return file
    }
}

public struct ParamRecord: Decodable, Sendable {
    public var name: String
    public var type: String?
    public var direction: String?
    public var doc: String
    public var doc_html: String

    public init(name: String, type: String? = nil, direction: String? = nil,
                doc: String = "", doc_html: String = "") {
        self.name = name
        self.type = type
        self.direction = direction
        self.doc = doc
        self.doc_html = doc_html
    }
}

public struct SymbolRecord: Decodable, Sendable {
    public var id: String
    public var lang: String
    public var kind: String
    public var name: String
    public var qualified: String
    public var parent: String?
    public var signature: String
    public var doc: String
    public var summary: String
    public var doc_html: String
    public var summary_html: String
    public var params: [ParamRecord]?
    public var returns: String?
    public var returns_html: String?
    public var file: String?
    public var line: Int?

    public init(id: String, lang: String, kind: String, name: String,
                qualified: String, parent: String? = nil, signature: String = "",
                doc: String = "", summary: String = "", doc_html: String = "",
                summary_html: String = "", params: [ParamRecord]? = nil,
                returns: String? = nil, returns_html: String? = nil,
                file: String? = nil, line: Int? = nil) {
        self.id = id
        self.lang = lang
        self.kind = kind
        self.name = name
        self.qualified = qualified
        self.parent = parent
        self.signature = signature
        self.doc = doc
        self.summary = summary
        self.doc_html = doc_html
        self.summary_html = summary_html
        self.params = params
        self.returns = returns
        self.returns_html = returns_html
        self.file = file
        self.line = line
    }
}

public struct GuideRecord: Decodable, Sendable {
    public var slug: String
    public var title: String
    public var file: String
    public var html: String

    public init(slug: String, title: String, file: String = "", html: String) {
        self.slug = slug
        self.title = title
        self.file = file
        self.html = html
    }
}

public enum DocsError: Error, CustomStringConvertible {
    case unsupportedFormat(Int)

    public var description: String {
        switch self {
        case .unsupportedFormat(let f):
            return "api.json format \(f) is not supported (expected 1); rebuild it with `just docs-api`"
        }
    }
}
