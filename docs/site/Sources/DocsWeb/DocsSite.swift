// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

import DocsModel
import Foundation
import NPRPC

/// Holds the index built from api.json and rebuilds it when the file
/// changes, so `just docs-api` shows up on the next request without a
/// restart.
public final class DocsStore: @unchecked Sendable {
    private let path: String
    private let lock = NSLock()
    private var index: DocsIndex
    private var loadedAt: Date?

    public init(path: String) throws {
        self.path = path
        self.index = DocsIndex(try ApiFile.load(from: path))
        self.loadedAt = Self.modified(path)
    }

    public var current: DocsIndex {
        lock.lock()
        defer { lock.unlock() }
        let modified = Self.modified(path)
        if let modified, modified != loadedAt {
            // A half-written or broken file keeps the last good index.
            do {
                index = DocsIndex(try ApiFile.load(from: path))
                loadedAt = modified
            } catch {
                FileHandle.standardError.write(Data("docs: keeping previous api.json: \(error)\n".utf8))
            }
        }
        return index
    }

    private static func modified(_ path: String) -> Date? {
        (try? FileManager.default.attributesOfItem(atPath: path))?[.modificationDate] as? Date
    }
}

public struct DocsSite: Sendable {
    public static let dropdownResults = 12
    public static let pageResults = 100

    private let store: DocsStore
    private let library: TemplateLibrary

    public init(store: DocsStore, templateDirectory: String, hotReload: Bool = false) throws {
        self.store = store
        self.library = try TemplateLibrary(directory: templateDirectory, hotReload: hotReload)
    }

    public var templateNames: [String] { library.templateNames }

    /// Render one request, or decline it so NPRPC serves static files.
    public func handle(_ request: PageRequest) -> PageResponse? {
        guard request.method == "GET" || request.method == "HEAD" else { return nil }
        let index = store.current
        // htmx navigation swaps #content, so it needs the page body alone. A
        // history restore wants the whole document back.
        let fragment = request.headers["hx-request"] == "true"
            && request.headers["hx-history-restore-request"] != "true"

        let parts = request.path.split(separator: "/").map {
            String($0).removingPercentEncoding ?? String($0)
        }

        switch parts.first {
        case nil:
            let view = HomeView(
                langs: Language.all.map { lang in
                    let (symbols, documented) = index.stats(lang: lang.id)
                    return LangCard(title: lang.title, url: "/api/" + lang.id,
                                    symbols: symbols, documented: documented)
                },
                guides: guideNav(index))
            return page("home", view, title: "NPRPC documentation", index: index, fragment: fragment)

        case "api":
            guard parts.count >= 2, let language = Language.named(parts[1]) else {
                return notFound(index: index, fragment: fragment)
            }
            if parts.count == 2 {
                return page("lang", LangView(language: language, index: index),
                            title: "\(language.title) API", index: index, fragment: fragment)
            }
            guard let p = index.page(lang: language.id, components: Array(parts.dropFirst(2))) else {
                return notFound(index: index, fragment: fragment)
            }
            let view = SymbolPageView(page: p, index: index)
            return page("symbol", view, title: "\(p.components.joined(separator: language.separator)) — \(language.title)",
                        index: index, fragment: fragment)

        case "guide":
            guard parts.count == 2, let guide = index.guide(slug: parts[1]) else {
                return notFound(index: index, fragment: fragment)
            }
            return page("guide", GuideView(title: guide.title, html: guide.html, file: guide.file),
                        title: guide.title, index: index, fragment: fragment)

        case "search":
            let query = request.queryItems["q"] ?? ""
            // The header box asks for a short list to drop down; a submitted
            // search (or no JavaScript) gets a full results page.
            if request.headers["hx-target"] == "search-results" {
                let view = SearchView(query: query, index: index, limit: Self.dropdownResults)
                return render("partials/search_results", view)
            }
            let view = SearchView(query: query, index: index, limit: Self.pageResults)
            return page("search", view, title: "Search: \(query)", index: index,
                        fragment: fragment, query: query)

        default:
            // Assets (/style.css, /vendor/htmx.min.js) go to the static root.
            if parts.last?.contains(".") == true { return nil }
            return notFound(index: index, fragment: fragment)
        }
    }

    // MARK: - Rendering

    private func guideNav(_ index: DocsIndex) -> [NavItem] {
        index.guides.map { NavItem(title: $0.title, url: "/guide/" + $0.slug) }
    }

    private func page(_ template: String, _ view: Any, title: String, index: DocsIndex,
                      fragment: Bool, query: String = "", status: Int = 200) -> PageResponse {
        guard let content = library.render(view, withTemplate: template) else {
            return templateMissing(template)
        }
        let fullTitle = title == "NPRPC documentation" ? title : "\(title) · NPRPC"
        if fragment {
            // htmx takes the document title from a <title> in the swapped
            // content.
            return PageResponse(html: "<title>\(escape(fullTitle))</title>\n" + content, status: status)
        }
        let layout = LayoutView(
            title: fullTitle, content: content,
            langs: Language.all.map { NavItem(title: $0.title, url: "/api/" + $0.id) },
            guides: guideNav(index), query: query)
        guard let html = library.render(layout, withTemplate: "layout") else {
            return templateMissing("layout")
        }
        return PageResponse(html: html, status: status)
    }

    private func render(_ template: String, _ view: Any) -> PageResponse {
        guard let html = library.render(view, withTemplate: template) else {
            return templateMissing(template)
        }
        return PageResponse(html: html)
    }

    private func notFound(index: DocsIndex, fragment: Bool) -> PageResponse {
        page("not_found", NavItem(title: "", url: ""), title: "Not found", index: index,
             fragment: fragment, status: 404)
    }

    private func templateMissing(_ name: String) -> PageResponse {
        PageResponse(html: "<h1>500</h1><p>Template '\(escape(name))' is missing.</p>", status: 500)
    }

    private func escape(_ s: String) -> String {
        s.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
    }
}
