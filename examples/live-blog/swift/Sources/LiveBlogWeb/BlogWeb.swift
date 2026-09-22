// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// The live-blog page handler: HTML rendered in the process that owns the data.
//
// Routes the page requests NPRPC hands over, pulls what they need straight from
// the repository, and renders Mustache.  Nothing here goes over the wire — the
// same values the RPC servants return are used directly.

import Foundation
import LiveBlogAPI
import Mustache
import NPRPC
import NPRPCWeb

/// What the page handler needs from the application's data layer.
///
/// Deliberately the same shape as `BlogServiceProtocol`, so the server's
/// existing repository satisfies it as-is and the pages and the RPC endpoint
/// cannot drift apart.
public protocol BlogDataSource: Sendable {
    func listPosts(page: UInt32, pageSize: UInt32) -> PostPage
    func getPost(slug: String) -> PostDetail
    func listComments(postId: UInt64, page: UInt32, pageSize: UInt32) -> [Comment]
    func listAuthorPosts(authorSlug: String, page: UInt32, pageSize: UInt32) -> [PostPreview]
    func getAuthor(authorSlug: String) -> AuthorPreview
}

public struct BlogWeb: Sendable {
    public static let postsPerPage: UInt32 = 5
    public static let commentsPerPage: UInt32 = 20

    private let library: TemplateLibrary
    private let data: BlogDataSource

    /// - Parameters:
    ///   - templateDirectory: directory of `.mustache` files; names are the
    ///     relative path without the extension, so `partials/post_cards.mustache`
    ///     is referenced as `{{> partials/post_cards}}`.
    ///   - hotReload: re-read templates from disk on every render.  Cheap enough
    ///     for development, wasteful in production.
    public init(templateDirectory: String,
                data: BlogDataSource,
                hotReload: Bool = false) throws {
        self.library = try TemplateLibrary(directory: templateDirectory,
                                           hotReload: hotReload)
        self.data = data
    }

    /// Template names loaded, for startup logging.
    public var templateNames: [String] { library.templateNames }

    /// Render one request, or decline it so NPRPC falls through to static files.
    public func handle(_ request: PageRequest) -> PageResponse? {
        guard request.method == "GET" || request.method == "HEAD" else { return nil }

        // htmx swaps the target element's contents, so those requests get the
        // partial alone.  Same route, same data, one less layer.
        let isFragment = request.headers["hx-request"] == "true"

        switch route(for: request.path) {
        case .home:
            return .redirect(to: "/blog?page=1", status: 302)

        case .blog:
            let page = pageNumber(from: request)
            let model = BlogListView(
                data.listPosts(page: page, pageSize: Self.postsPerPage))
            if isFragment {
                return render("partials/post_cards", model)
            }
            return renderPage("blog", model, title: "Blog")

        case .post(let slug):
            let post = data.getPost(slug: slug)
            guard post.id != 0 else { return notFound(slug: slug) }
            let comments = data.listComments(postId: post.id, page: 1,
                                             pageSize: Self.commentsPerPage)
            let model = PostView(post, comments: comments)
            if isFragment {
                return render("partials/comments", model)
            }
            return renderPage("post", model, title: post.title)

        case .author(let slug):
            let author = data.getAuthor(authorSlug: slug)
            guard author.id != 0 else { return notFound(slug: slug) }
            let page = pageNumber(from: request)
            let posts = data.listAuthorPosts(authorSlug: slug, page: page,
                                             pageSize: Self.postsPerPage)
            let model = AuthorView(author: author, posts: posts, page: page)
            if isFragment {
                return render("partials/post_cards", model)
            }
            return renderPage("author", model, title: author.name)

        case .none:
            // Not a page route: assets, /host.json, anything else the C++ file
            // cache should serve.
            return nil
        }
    }

    // MARK: - Routing

    private enum Route {
        case home
        case blog
        case post(String)
        case author(String)
    }

    private func route(for path: String) -> Route? {
        if path == "/" { return .home }
        if path == "/blog" { return .blog }

        let parts = path.split(separator: "/", omittingEmptySubsequences: true)
        guard parts.count == 2 else { return nil }
        // A slug with a dot is a filename, not a route — don't shadow the
        // static file cache with a 404 page.
        let slug = String(parts[1])
        guard !slug.contains(".") else { return nil }

        switch parts[0] {
        case "post": return .post(slug)
        case "author": return .author(slug)
        default: return nil
        }
    }

    private func pageNumber(from request: PageRequest) -> UInt32 {
        guard let raw = request.queryItems["page"], let value = UInt32(raw) else {
            return 1
        }
        return max(value, 1)
    }

    // MARK: - Rendering

    private func render(_ template: String, _ model: Any) -> PageResponse? {
        guard let html = library.render(model, withTemplate: template) else {
            return PageResponse(
                html: "<h1>500</h1><p>Template '\(template)' is missing.</p>",
                status: 500)
        }
        return PageResponse(html: html)
    }

    private func renderPage(_ template: String,
                            _ model: Any,
                            title: String) -> PageResponse? {
        guard let content = library.render(model, withTemplate: template) else {
            return PageResponse(
                html: "<h1>500</h1><p>Template '\(template)' is missing.</p>",
                status: 500)
        }
        return render("layout", LayoutView(page_title: title,
                                           nav: Self.nav,
                                           content: content))
    }

    private func notFound(slug: String) -> PageResponse {
        let body = """
            <div class="glass-card p-8">
              <p class="eyebrow">Not found</p>
              <h1 class="mt-4 text-3xl font-semibold text-stone-950">\(escape(slug))</h1>
              <p class="mt-4 text-stone-700">Nothing in the example dataset matches that.</p>
              <p class="mt-6"><a class="text-amber-800 hover:underline" href="/blog?page=1">Back to the blog</a></p>
            </div>
            """
        guard let html = library.render(
            LayoutView(page_title: "Not found", nav: Self.nav, content: body),
            withTemplate: "layout") else {
            return PageResponse(html: "<h1>404</h1>", status: 404)
        }
        return PageResponse(html: html, status: 404)
    }

    private func escape(_ s: String) -> String {
        s.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
    }

    private static let nav = [
        LayoutView.NavItem(href: "/blog?page=1", label: "Blog"),
        LayoutView.NavItem(href: "/post/first-steps-with-nprpc", label: "Post"),
        LayoutView.NavItem(href: "/author/editorial-desk", label: "Author"),
    ]
}
