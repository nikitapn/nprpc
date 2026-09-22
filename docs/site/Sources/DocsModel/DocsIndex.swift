// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import Foundation

public struct Language: Sendable {
    public let id: String
    public let title: String
    /// Separator between the parts of a qualified name.
    public let separator: String

    public static let all: [Language] = [
        Language(id: "idl", title: "IDL", separator: "."),
        Language(id: "swift", title: "Swift", separator: "."),
        Language(id: "cpp", title: "C++", separator: "::"),
    ]

    public static func named(_ id: String) -> Language? {
        all.first { $0.id == id }
    }
}

/// A URL of its own: a top-level declaration, or one with members. Overloads
/// of a free function share one page.
public struct Page: Sendable {
    public let key: String
    public let lang: String
    public let components: [String]
    /// The declarations the page documents, in source order.
    public let symbolIds: [String]

    public var title: String { components.last ?? key }
    public var namespace: [String] { Array(components.dropLast()) }
}

public struct SearchHit: Sendable {
    public let symbol: SymbolRecord
    public let url: String
}

/// Everything the site needs, computed once from api.json: which symbols
/// get pages, every symbol's URL, member lists, cross-links in docs, and
/// search.
public struct DocsIndex: Sendable {
    // Written twice in init: raw first, so the cross-linker can resolve
    // against them, then with the links in.
    public private(set) var symbols: [String: SymbolRecord]
    public private(set) var guides: [GuideRecord]
    public let pages: [String: Page]

    private let children: [String: [String]]
    private let pageKeyOf: [String: String]
    private let anchorOf: [String: String]
    /// lang -> name -> ids, for resolving `Name` in docs and for search.
    private let byName: [String: [String: [String]]]

    public init(_ api: ApiFile) {
        var symbols: [String: SymbolRecord] = [:]
        for s in api.symbols { symbols[s.id] = s }

        // A parent that is not in the file (a Swift extension of another
        // module's type) cannot hold members; such symbols stand alone.
        var children: [String: [String]] = [:]
        for s in api.symbols {
            if let p = s.parent, symbols[p] != nil {
                children[p, default: []].append(s.id)
            } else if s.parent != nil {
                symbols[s.id]?.parent = nil
            }
        }
        for (key, ids) in children {
            children[key] = ids.sorted { Self.memberOrder(symbols[$0]!, symbols[$1]!) }
        }

        // Pages: top-level declarations, and anything with members.
        var pageMembers: [String: [String]] = [:]
        var pageLang: [String: (String, [String])] = [:]
        var pageKeyOf: [String: String] = [:]
        for s in symbols.values where s.parent == nil || children[s.id] != nil {
            guard let lang = Language.named(s.lang) else { continue }
            let components = s.qualified.components(separatedBy: lang.separator)
            let key = s.lang + "/" + components.joined(separator: "/")
            pageMembers[key, default: []].append(s.id)
            pageLang[key] = (s.lang, components)
            pageKeyOf[s.id] = key
        }
        var pages: [String: Page] = [:]
        for (key, ids) in pageMembers {
            let (lang, components) = pageLang[key]!
            let ordered = ids.sorted { Self.sourceOrder(symbols[$0]!, symbols[$1]!) }
            pages[key] = Page(key: key, lang: lang, components: components, symbolIds: ordered)
        }

        // Anchors are unique within a page: the page's own overloads first,
        // then its members.
        var anchorOf: [String: String] = [:]
        for page in pages.values {
            var used: [String: Int] = [:]
            func assign(_ id: String) {
                let base = Self.slug(symbols[id]!.name)
                let n = used[base, default: 0] + 1
                used[base] = n
                anchorOf[id] = n == 1 ? base : "\(base)-\(n)"
            }
            page.symbolIds.forEach(assign)
            for id in page.symbolIds {
                children[id]?.forEach(assign)
            }
        }

        var byName: [String: [String: [String]]] = [:]
        for s in symbols.values {
            byName[s.lang, default: [:]][s.name, default: []].append(s.id)
        }

        self.children = children
        self.pages = pages
        self.pageKeyOf = pageKeyOf
        self.anchorOf = anchorOf
        self.byName = byName
        self.symbols = symbols
        self.guides = api.guides

        // Second pass: now that every symbol has a URL, turn `Name` in docs
        // into links. The linker reads the index, so it runs on a copy.
        let linker = CrossLinker(index: self)
        var linked = symbols
        for (id, s) in symbols {
            var s = s
            s.doc_html = linker.link(s.doc_html, lang: s.lang, from: id)
            s.summary_html = linker.link(s.summary_html, lang: s.lang, from: id)
            if let r = s.returns_html {
                s.returns_html = linker.link(r, lang: s.lang, from: id)
            }
            if var params = s.params {
                for i in params.indices {
                    params[i].doc_html = linker.link(params[i].doc_html, lang: s.lang, from: id)
                }
                s.params = params
            }
            linked[id] = s
        }
        let slugs = Set(api.guides.map(\.slug))
        self.symbols = linked
        self.guides = api.guides.map { g in
            var g = g
            g.html = linker.link(Self.rewriteGuideLinks(g.html, slugs: slugs),
                                 lang: nil, from: nil)
            return g
        }
    }

    // MARK: - Lookup

    public func page(lang: String, components: [String]) -> Page? {
        pages[lang + "/" + components.joined(separator: "/")]
    }

    public func members(of id: String) -> [SymbolRecord] {
        (children[id] ?? []).compactMap { symbols[$0] }
    }

    public func hasPage(_ id: String) -> Bool { pageKeyOf[id] != nil }

    public func anchor(of id: String) -> String { anchorOf[id] ?? Self.slug(id) }

    public func url(of id: String) -> String? {
        if let key = pageKeyOf[id], let page = pages[key] {
            let path = "/api/" + page.lang + "/" + page.components.map(Self.encode).joined(separator: "/")
            // Overloads share a page; each has its own anchor on it.
            return page.symbolIds.count > 1 ? path + "#" + anchor(of: id) : path
        }
        guard let parent = symbols[id]?.parent, let base = url(of: parent) else { return nil }
        let pagePath = base.split(separator: "#", maxSplits: 1).first.map(String.init) ?? base
        return pagePath + "#" + anchor(of: id)
    }

    public func pageURL(_ page: Page) -> String {
        "/api/" + page.lang + "/" + page.components.map(Self.encode).joined(separator: "/")
    }

    /// Top-level pages of a language, grouped by namespace (module for
    /// Swift), namespaces and names sorted.
    public func topLevelPages(lang: String) -> [(namespace: String, pages: [Page])] {
        guard let language = Language.named(lang) else { return [] }
        var groups: [String: [Page]] = [:]
        for page in pages.values where page.lang == lang {
            guard page.symbolIds.contains(where: { symbols[$0]?.parent == nil }) else { continue }
            groups[page.namespace.joined(separator: language.separator), default: []].append(page)
        }
        return groups.keys.sorted().map { ns in
            (ns, groups[ns]!.sorted { $0.title.lowercased() < $1.title.lowercased() })
        }
    }

    public func stats(lang: String) -> (symbols: Int, documented: Int) {
        let all = symbols.values.filter { $0.lang == lang }
        return (all.count, all.filter { !$0.doc.isEmpty }.count)
    }

    public func guide(slug: String) -> GuideRecord? {
        guides.first { $0.slug == slug }
    }

    // MARK: - Search

    /// Names matching `query`: exact first, then prefix, then substring of
    /// the name or qualified name. Declarations with pages rank above members.
    public func search(_ query: String, limit: Int = 40) -> [SearchHit] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        guard !q.isEmpty else { return [] }

        var scored: [(Int, SymbolRecord)] = []
        for s in symbols.values {
            let name = s.name.lowercased()
            let score: Int
            if name == q { score = 0 }
            else if name.hasPrefix(q) { score = 1 }
            else if name.contains(q) { score = 2 }
            else if s.qualified.lowercased().contains(q) { score = 3 }
            else { continue }
            scored.append((score * 2 + (hasPage(s.id) ? 0 : 1), s))
        }
        let langRank = Dictionary(uniqueKeysWithValues: Language.all.enumerated().map { ($1.id, $0) })
        scored.sort { a, b in
            if a.0 != b.0 { return a.0 < b.0 }
            if a.1.qualified.count != b.1.qualified.count { return a.1.qualified.count < b.1.qualified.count }
            if a.1.lang != b.1.lang { return langRank[a.1.lang, default: 9] < langRank[b.1.lang, default: 9] }
            return a.1.id < b.1.id
        }
        return scored.prefix(limit).compactMap { _, s in
            url(of: s.id).map { SearchHit(symbol: s, url: $0) }
        }
    }

    // MARK: - Cross-link resolution

    /// The symbol a code span like `PoaBuilder`, `PoaBuilder::build` or
    /// `build()` names, if exactly one fits. `lang` nil searches every
    /// language, for guides.
    func resolve(_ reference: String, lang: String?) -> String? {
        var ref = reference.trimmingCharacters(in: .whitespaces)
        if let paren = ref.firstIndex(of: "("), ref.hasSuffix(")") {
            ref = String(ref[..<paren])
        }
        guard !ref.isEmpty,
              ref.allSatisfy({ $0.isLetter || $0.isNumber || $0 == "_" || $0 == ":" || $0 == "." || $0 == "~" })
        else { return nil }

        let langs = lang.map { [$0] } ?? Language.all.map(\.id)
        var candidates: [SymbolRecord] = []
        for l in langs {
            guard let language = Language.named(l) else { continue }
            let parts = ref.replacingOccurrences(of: "::", with: ".")
                .split(separator: ".").map(String.init)
            guard let last = parts.last else { continue }
            let suffix = parts.joined(separator: language.separator)
            for id in byName[l]?[last] ?? [] {
                guard let s = symbols[id] else { continue }
                if parts.count == 1 || s.qualified == suffix
                    || s.qualified.hasSuffix(language.separator + suffix) {
                    candidates.append(s)
                }
            }
        }
        if candidates.count > 1 {
            let topLevel = candidates.filter { $0.parent == nil }
            if !topLevel.isEmpty { candidates = topLevel }
        }
        // Overloads of one function all land on the same page.
        let keys = Set(candidates.map { pageKeyOf[$0.id] ?? $0.id })
        guard keys.count == 1, let first = candidates.min(by: { $0.id < $1.id }) else { return nil }
        return first.id
    }

    // MARK: - Helpers

    static func slug(_ s: String) -> String {
        var out = ""
        var dash = false
        for c in s.unicodeScalars {
            if CharacterSet.alphanumerics.contains(c) || c == "_" {
                out.unicodeScalars.append(c)
                dash = false
            } else if !dash && !out.isEmpty {
                out.append("-")
                dash = true
            }
        }
        while out.hasSuffix("-") { out.removeLast() }
        return out.isEmpty ? "op" : out
    }

    static func encode(_ component: String) -> String {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-_.~")
        return component.addingPercentEncoding(withAllowedCharacters: allowed) ?? component
    }

    private static let kindRank: [String: Int] = [
        "constructor": 0, "case": 1, "field": 2, "property": 2, "variable": 3,
        "method": 4, "operator": 5, "function": 6, "typealias": 7,
    ]

    private static func memberOrder(_ a: SymbolRecord, _ b: SymbolRecord) -> Bool {
        let ra = kindRank[a.kind, default: 8], rb = kindRank[b.kind, default: 8]
        if ra != rb { return ra < rb }
        // Enum cases keep their declared order; everything else is alphabetical.
        if a.kind == "case" { return sourceOrder(a, b) }
        if a.name != b.name { return a.name.lowercased() < b.name.lowercased() }
        return sourceOrder(a, b)
    }

    private static func sourceOrder(_ a: SymbolRecord, _ b: SymbolRecord) -> Bool {
        if a.file != b.file { return (a.file ?? "") < (b.file ?? "") }
        if a.line != b.line { return (a.line ?? 0) < (b.line ?? 0) }
        return a.id < b.id
    }

    /// `href="BUILD.md#x"` between guides becomes `/guide/BUILD#x`.
    static func rewriteGuideLinks(_ html: String, slugs: Set<String>) -> String {
        var out = ""
        var rest = Substring(html)
        while let r = rest.range(of: "href=\"") {
            out += rest[..<r.upperBound]
            rest = rest[r.upperBound...]
            guard let end = rest.firstIndex(of: "\"") else { break }
            var target = String(rest[..<end])
            if !target.contains("://") {
                let parts = target.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)
                var file = String(parts[0])
                if file.hasPrefix("./") { file.removeFirst(2) }
                if file.hasSuffix(".md") {
                    let slug = String(file.dropLast(3))
                    if slugs.contains(slug) {
                        target = "/guide/" + slug + (parts.count > 1 ? "#" + parts[1] : "")
                    }
                }
            }
            out += target
            rest = rest[end...]
        }
        out += rest
        return out
    }
}

/// Wraps inline `<code>Name</code>` spans that name a known symbol in a link
/// to it. Code blocks (`<pre><code>`) are left alone.
struct CrossLinker {
    let index: DocsIndex

    func link(_ html: String, lang: String?, from self_: String?) -> String {
        guard html.contains("<code>") else { return html }
        var out = ""
        var rest = Substring(html)
        while let open = rest.range(of: "<code>") {
            out += rest[..<open.lowerBound]
            guard let close = rest[open.upperBound...].range(of: "</code>") else {
                out += rest[open.lowerBound...]
                return out
            }
            let inner = String(rest[open.upperBound..<close.lowerBound])
            let span = String(rest[open.lowerBound..<close.upperBound])
            rest = rest[close.upperBound...]

            // Block code, or a span the author already put inside a link.
            if out.hasSuffix("<pre>") || Self.insideLink(out) {
                out += span
                continue
            }
            if let id = index.resolve(Self.unescape(inner), lang: lang), id != self_,
               let url = index.url(of: id) {
                out += "<a class=\"xref\" href=\"\(url)\">" + span + "</a>"
            } else {
                out += span
            }
        }
        out += rest
        return out
    }

    static func insideLink(_ html: String) -> Bool {
        guard let open = html.range(of: "<a ", options: .backwards) else { return false }
        guard let close = html.range(of: "</a>", options: .backwards) else { return true }
        return open.lowerBound > close.lowerBound
    }

    static func unescape(_ s: String) -> String {
        s.replacingOccurrences(of: "&lt;", with: "<")
            .replacingOccurrences(of: "&gt;", with: ">")
            .replacingOccurrences(of: "&quot;", with: "\"")
            .replacingOccurrences(of: "&amp;", with: "&")
    }
}
