// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// View models for the Mustache templates.
//
// swift-mustache resolves `{{field}}` by reflecting over an object's *stored*
// properties, so the npidl-generated structs work as template contexts with no
// mapping code at all.  These wrappers exist only where a template needs
// something the generated type does not carry — a formatted date, a link — and
// they store it rather than compute it, because a computed property is
// invisible to Mirror.

import Foundation
import LiveBlogAPI
import Mustache

/// An ISO-8601 timestamp that renders as a readable date.
///
/// `{{published}}` gives "9 Mar 2026"; `{{iso(published)}}` gives the original
/// string, for a `<time datetime="...">` attribute.
public struct IsoDate: MustacheCustomRenderable, MustacheTransformable, Sendable {
    public let raw: String

    public init(_ raw: String) { self.raw = raw }

    private static let months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    /// "2026-03-09T09:00:00Z" -> "9 Mar 2026".
    ///
    /// Reads the date part directly instead of going through DateFormatter:
    /// the formatters are not Sendable and this runs on several HTTP threads at
    /// once, so sharing one would mean a lock on the render path for no gain on
    /// a fixed, known format.
    ///
    /// A timestamp that does not parse renders as-is — a malformed date in the
    /// dataset should look wrong on the page, not vanish from it.
    public var renderText: String {
        let parts = raw.prefix(10).split(separator: "-")
        guard parts.count == 3,
              let year = Int(parts[0]),
              let month = Int(parts[1]), (1...12).contains(month),
              let day = Int(parts[2]), (1...31).contains(day)
        else { return raw }
        return "\(day) \(Self.months[month - 1]) \(year)"
    }

    // The protocol's default implementations are internal to the Mustache
    // module, so both requirements have to be spelled out here.
    public var isNull: Bool { false }

    public func transform(_ name: String) -> Any? {
        name == "iso" ? raw : nil
    }
}

/// One post in a listing.
public struct PostCard {
    public let id: UInt64
    public let slug: String
    public let title: String
    public let excerpt: String
    public let cover_url: String?
    public let author: AuthorPreview
    public let published: IsoDate
    public let href: String

    public init(_ post: PostPreview) {
        id = post.id
        slug = post.slug
        title = post.title
        excerpt = post.excerpt
        cover_url = post.cover_url
        author = post.author
        published = IsoDate(post.published_at)
        href = "/post/\(post.slug)"
    }
}

/// `blog.mustache` and `partials/post_cards.mustache`.
public struct BlogListView {
    public let posts: [PostCard]
    public let page: UInt32
    public let total_posts: UInt32
    public let total_pages: UInt32
    public let has_prev: Bool
    public let has_next: Bool

    public init(_ page: PostPage) {
        self.posts = page.posts.map(PostCard.init)
        self.page = page.page
        self.total_posts = page.total_posts
        self.total_pages = page.page_size > 0
            ? (page.total_posts + page.page_size - 1) / page.page_size
            : 0
        self.has_prev = page.page > 1
        self.has_next = page.page < self.total_pages
    }
}

/// `author.mustache` — an author header above the same post-card partial.
public struct AuthorView {
    public let author: AuthorPreview
    public let posts: [PostCard]
    public let page: UInt32
    public let total_pages: UInt32
    public let has_prev: Bool
    public let has_next: Bool

    public init(author: AuthorPreview, posts: [PostPreview], page: UInt32) {
        self.author = author
        self.posts = posts.map(PostCard.init)
        self.page = page
        // The author listing has no total count in the contract, so paging
        // forward is offered only while the current page came back full.
        self.total_pages = page
        self.has_prev = page > 1
        self.has_next = false
    }
}

/// `partials/comments.mustache`.
public struct CommentView {
    public let id: UInt64
    public let author_name: String
    public let body: String
    public let created: IsoDate

    public init(_ comment: Comment) {
        id = comment.id
        author_name = comment.author_name
        body = comment.body
        created = IsoDate(comment.created_at)
    }
}

/// `post.mustache`.
public struct PostView {
    public let id: UInt64
    public let slug: String
    public let title: String
    public let summary: String
    public let body_html: String
    public let author: AuthorPreview
    public let tags: [String]
    public let published: IsoDate
    public let comments: [CommentView]

    public init(_ post: PostDetail, comments: [Comment]) {
        id = post.id
        slug = post.slug
        title = post.title
        summary = post.summary
        body_html = post.body_html
        author = post.author
        tags = post.tags
        published = IsoDate(post.published_at)
        self.comments = comments.map(CommentView.init)
    }
}

/// `layout.mustache`.
struct LayoutView {
    struct NavItem {
        let href: String
        let label: String
    }

    let page_title: String
    let nav: [NavItem]
    /// Rendered page body, injected with `{{{content}}}` — already HTML.
    let content: String
}
