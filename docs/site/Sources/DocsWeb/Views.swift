// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import DocsModel

// Mustache contexts. swift-mustache reads stored properties by name, so the
// field names here are the names templates use.
//
// Text a template tests with `{{#field}}` is optional: swift-mustache treats
// an empty string as true, so "no doc" has to be nil to reach `{{^field}}`.

extension String {
    var nonEmpty: String? { isEmpty ? nil : self }
}

struct NavItem {
    let title: String
    let url: String
}

struct LayoutView {
    let title: String
    let content: String
    let langs: [NavItem]
    let guides: [NavItem]
    let query: String
}

struct ParamView {
    let name: String
    let type: String?
    let direction: String?
    let doc_html: String
}

/// One declaration: a page's own symbol, or a member listed on it.
struct DeclView {
    let anchor: String
    let kind: String
    let name: String
    let signature: String
    /// highlight.js language of the signature (see web/code.js).
    let code_lang: String
    let doc_html: String?
    let summary_html: String?
    let params: [ParamView]
    let has_params: Bool
    let returns_html: String?
    let location: String?
    /// Set when the member has a page of its own; the listing then links to
    /// it with the summary instead of repeating the whole entry.
    let url: String?

    init(_ s: SymbolRecord, index: DocsIndex, linkToOwnPage: Bool) {
        anchor = index.anchor(of: s.id)
        kind = s.kind
        name = s.name
        signature = s.signature
        code_lang = s.lang == "idl" ? "npidl" : s.lang
        doc_html = s.doc_html.nonEmpty
        summary_html = s.summary_html.nonEmpty
        // Undocumented parameters add a table of names the signature already
        // shows; list them only when at least one says something.
        let ps = s.params ?? []
        params = ps.map {
            ParamView(name: $0.name, type: $0.type, direction: $0.direction, doc_html: $0.doc_html)
        }
        has_params = ps.contains { !$0.doc.isEmpty }
        returns_html = s.returns_html?.nonEmpty
        if let file = s.file {
            location = s.line.map { "\(file):\($0)" } ?? file
        } else {
            location = nil
        }
        url = linkToOwnPage && index.hasPage(s.id) ? index.url(of: s.id) : nil
    }
}

struct MemberGroupView {
    let title: String
    let members: [DeclView]
}

struct Crumb {
    let title: String
    let url: String
}

struct SymbolPageView {
    let lang_title: String
    let title: String
    let kind: String
    let crumbs: [Crumb]
    let decls: [DeclView]
    let groups: [MemberGroupView]

    private static let groupTitles: [String: String] = [
        "constructor": "Constructors", "case": "Cases", "field": "Fields",
        "property": "Properties", "variable": "Variables", "method": "Methods",
        "operator": "Operators", "function": "Functions", "typealias": "Type aliases",
    ]

    init(page: Page, index: DocsIndex) {
        let language = Language.named(page.lang)
        let decls = page.symbolIds.compactMap { index.symbols[$0] }
        lang_title = language?.title ?? page.lang
        title = page.title
        kind = decls.first?.kind ?? ""
        self.decls = decls.map { DeclView($0, index: index, linkToOwnPage: false) }

        // Breadcrumbs: language, then each enclosing namespace or type that
        // has a page of its own.
        var crumbs = [Crumb(title: lang_title, url: "/api/" + page.lang)]
        for n in 1..<max(page.components.count, 1) {
            if let p = index.page(lang: page.lang, components: Array(page.components.prefix(n))) {
                crumbs.append(Crumb(title: p.title, url: index.pageURL(p)))
            } else {
                crumbs.append(Crumb(title: page.components[n - 1], url: "/api/\(page.lang)#ns-\(page.components.prefix(n).joined(separator: "-"))"))
            }
        }
        self.crumbs = crumbs

        // Members arrive sorted by kind; consecutive kinds with the same
        // heading form one group.
        var groups: [MemberGroupView] = []
        var current: (title: String, members: [DeclView])?
        for decl in decls {
            for m in index.members(of: decl.id) {
                let title = Self.groupTitles[m.kind] ?? "Types"
                let view = DeclView(m, index: index, linkToOwnPage: true)
                if current?.title == title {
                    current!.members.append(view)
                } else {
                    if let c = current { groups.append(MemberGroupView(title: c.title, members: c.members)) }
                    current = (title, [view])
                }
            }
        }
        if let c = current { groups.append(MemberGroupView(title: c.title, members: c.members)) }
        self.groups = groups
    }
}

struct LangItemView {
    let name: String
    let kind: String
    let summary_html: String?
    let url: String
}

struct NamespaceView {
    let name: String
    let anchor: String
    let items: [LangItemView]
}

struct LangView {
    let title: String
    let symbols: Int
    let documented: Int
    let namespaces: [NamespaceView]

    init(language: Language, index: DocsIndex) {
        title = language.title
        (symbols, documented) = index.stats(lang: language.id)
        namespaces = index.topLevelPages(lang: language.id).map { group in
            NamespaceView(
                name: group.namespace.isEmpty ? "(global)" : group.namespace,
                anchor: "ns-" + group.namespace.replacingOccurrences(of: language.separator, with: "-"),
                items: group.pages.compactMap { page in
                    guard let first = page.symbolIds.first.flatMap({ index.symbols[$0] }) else { return nil }
                    // Overloads: the first documented one speaks for the page.
                    let summary = page.symbolIds.lazy
                        .compactMap { index.symbols[$0]?.summary_html }
                        .first { !$0.isEmpty } ?? ""
                    return LangItemView(name: page.title, kind: first.kind,
                                        summary_html: summary.nonEmpty, url: index.pageURL(page))
                })
        }
    }
}

struct LangCard {
    let title: String
    let url: String
    let symbols: Int
    let documented: Int
}

struct HomeView {
    let langs: [LangCard]
    let guides: [NavItem]
}

struct GuideView {
    let title: String
    let html: String
    let file: String
}

struct HitView {
    let name: String
    let qualified: String
    let kind: String
    let lang_title: String
    let summary_html: String?
    let url: String
}

struct SearchView {
    let query: String?
    let hits: [HitView]
    let guides: [NavItem]
    let has_results: Bool

    init(query: String, index: DocsIndex, limit: Int) {
        self.query = query.nonEmpty
        hits = index.search(query, limit: limit).map { hit in
            HitView(name: hit.symbol.name, qualified: hit.symbol.qualified,
                    kind: hit.symbol.kind,
                    lang_title: Language.named(hit.symbol.lang)?.title ?? hit.symbol.lang,
                    summary_html: hit.symbol.summary_html.nonEmpty, url: hit.url)
        }
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        guides = q.isEmpty ? [] : index.guides
            .filter { $0.title.lowercased().contains(q) || $0.slug.lowercased().contains(q) }
            .map { NavItem(title: $0.title, url: "/guide/" + $0.slug) }
        has_results = !hits.isEmpty || !guides.isEmpty
    }
}
