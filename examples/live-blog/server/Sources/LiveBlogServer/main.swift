import Dispatch
import Foundation
import LiveBlogAPI
import NPRPC

private struct BlogPostRecord: Sendable {
  let id: UInt64
  let slug: String
  let title: String
  let excerpt: String
  let summary: String
  let bodyHtml: String
  let publishedAt: String
  let coverUrl: String?
  let authorSlug: String
  let tags: [String]
}

private struct BlogCommentRecord: Sendable {
  let id: UInt64
  let postId: UInt64
  let authorName: String
  let body: String
  let createdAt: String
}

private struct BlogRepository: Sendable {
  let authorsBySlug: [String: AuthorPreview]
  let posts: [BlogPostRecord]
  let commentsByPostId: [UInt64: [BlogCommentRecord]]

  init() {
    let authors: [AuthorPreview] = [
      AuthorPreview(
        id: 1,
        slug: "editorial-desk",
        name: "Editorial Desk",
        bio: "A small newsroom team covering framework architecture, streaming workflows, and product notes.",
        avatar_url: "https://images.unsplash.com/photo-1494790108377-be9c29b29330?auto=format&fit=crop&w=320&q=80"
      ),
      AuthorPreview(
        id: 2,
        slug: "nikita-notes",
        name: "Nikita Notes",
        bio: "Short technical essays from the builder side of NPRPC.",
        avatar_url: "https://images.unsplash.com/photo-1500648767791-00dcc994a43e?auto=format&fit=crop&w=320&q=80"
      ),
      AuthorPreview(
        id: 3,
        slug: "transport-lab",
        name: "Transport Lab",
        bio: "Experiments and field reports across HTTP, WebSocket, QUIC, and stream-heavy flows.",
        avatar_url: "https://images.unsplash.com/photo-1506794778202-cad84cf45f1d?auto=format&fit=crop&w=320&q=80"
      )
    ]

    self.authorsBySlug = Dictionary(uniqueKeysWithValues: authors.map { ($0.slug, $0) })
    self.posts = [
      BlogPostRecord(
        id: 101,
        slug: "first-steps-with-nprpc",
        title: "First Steps With NPRPC",
        excerpt: "Why a route-aware shell plus typed browser hydration is a better starting point than pushing business logic into SSR.",
        summary: "A tour through the hybrid architecture used by this example.",
        bodyHtml: "<p>This example keeps <strong>business logic in Swift</strong> while SvelteKit renders the route-aware frame. The browser then performs the real typed RPC calls for post lists, post details, comments, and later chat streams.</p><p>That split avoids duplicating domain rules across Node and Swift, while still delivering fast first paint and route-specific shells.</p>",
        publishedAt: "2026-03-01T09:00:00Z",
        coverUrl: "https://images.unsplash.com/photo-1516321318423-f06f85e504b3?auto=format&fit=crop&w=1200&q=80",
        authorSlug: "editorial-desk",
        tags: ["architecture", "ssr", "swift"]
      ),
      BlogPostRecord(
        id: 102,
        slug: "routing-shells-and-real-data",
        title: "Routing Shells And Real Data",
        excerpt: "The shell already knows where the user is going. That is enough to render structure before hydration without rehosting the backend in Node.",
        summary: "Using SSR for structure, not for duplicating backend logic.",
        bodyHtml: "<p>For pages like <code>/blog?page=2</code> or <code>/post/first-steps-with-nprpc</code>, SSR can immediately render the frame, title, navigation, and loading placeholders. The actual data remains in Swift.</p>",
        publishedAt: "2026-03-02T09:00:00Z",
        coverUrl: nil,
        authorSlug: "nikita-notes",
        tags: ["svelte", "hydration", "rpc"]
      ),
      BlogPostRecord(
        id: 103,
        slug: "quic-for-browser-streams",
        title: "QUIC For Browser Streams",
        excerpt: "WebTransport is not the first step, but it becomes compelling once the regular blog flow is stable.",
        summary: "Why streaming belongs after the basic app flow is already typed and working.",
        bodyHtml: "<p>Once the blog and comments work over ordinary RPC, the same server can expose bidi chat streams and media streams without changing the high-level application model.</p>",
        publishedAt: "2026-03-03T09:00:00Z",
        coverUrl: "https://images.unsplash.com/photo-1518770660439-4636190af475?auto=format&fit=crop&w=1200&q=80",
        authorSlug: "transport-lab",
        tags: ["quic", "webtransport", "streams"]
      ),
      BlogPostRecord(
        id: 104,
        slug: "swift-service-boundaries",
        title: "Swift Service Boundaries",
        excerpt: "Generated API code belongs in its own target so the service implementation stays focused on domain work.",
        summary: "Separating generated contracts from handwritten service code keeps the example maintainable.",
        bodyHtml: "<p>The example now compiles generated Swift RPC types into a dedicated <code>LiveBlogAPI</code> target. That keeps the executable focused on repository code, transport configuration, and service activation.</p>",
        publishedAt: "2026-03-04T09:00:00Z",
        coverUrl: nil,
        authorSlug: "nikita-notes",
        tags: ["swift", "codegen", "structure"]
      ),
      BlogPostRecord(
        id: 105,
        slug: "comments-without-rest-overhead",
        title: "Comments Without REST Overhead",
        excerpt: "Comments are still just typed calls. The value is in sharing one contract surface across list, detail, and stream flows.",
        summary: "Regular request-response calls still matter in a stream-capable system.",
        bodyHtml: "<p>Not every interaction should be a stream. Post detail and comment pagination are still better expressed as ordinary methods with typed return values.</p>",
        publishedAt: "2026-03-05T09:00:00Z",
        coverUrl: "https://images.unsplash.com/photo-1498050108023-c5249f4df085?auto=format&fit=crop&w=1200&q=80",
        authorSlug: "editorial-desk",
        tags: ["comments", "rpc", "product"]
      ),
      BlogPostRecord(
        id: 106,
        slug: "from-blog-to-live-media",
        title: "From Blog To Live Media",
        excerpt: "A realistic showcase starts with posts and comments, then grows into chat and media with the same runtime underneath.",
        summary: "The example roadmap from static-looking content to truly live transports.",
        bodyHtml: "<p>The purpose of this demo is to make the framework story legible. Pagination proves regular RPC, chat proves bidi streams, and later media proves long-lived server streams.</p>",
        publishedAt: "2026-03-06T09:00:00Z",
        coverUrl: nil,
        authorSlug: "transport-lab",
        tags: ["roadmap", "media", "demo"]
      )
    ]

    let comments: [BlogCommentRecord] = [
      BlogCommentRecord(id: 1001, postId: 101, authorName: "Ada", body: "Keeping domain logic out of SSR is the right constraint here.", createdAt: "2026-03-01T11:30:00Z"),
      BlogCommentRecord(id: 1002, postId: 101, authorName: "Sam", body: "The route-aware shell idea makes the architecture much easier to explain.", createdAt: "2026-03-01T12:10:00Z"),
      BlogCommentRecord(id: 1003, postId: 101, authorName: "Mina", body: "A generated client contract for list and detail calls already makes this feel like a product, not a transport benchmark.", createdAt: "2026-03-01T12:45:00Z"),
      BlogCommentRecord(id: 1004, postId: 103, authorName: "Leo", body: "Good call on stabilizing normal RPC before adding bidi chat and media.", createdAt: "2026-03-03T13:20:00Z"),
      BlogCommentRecord(id: 1005, postId: 105, authorName: "June", body: "Typed comments are enough to prove the regular request-response path.", createdAt: "2026-03-05T14:05:00Z")
    ]

    self.commentsByPostId = Dictionary(grouping: comments, by: \.postId)
  }

  func listPosts(page: UInt32, pageSize: UInt32) -> PostPage {
    let normalizedPage = max(page, 1)
    let normalizedPageSize = max(pageSize, 1)
    let start = Int((normalizedPage - 1) * normalizedPageSize)
    let end = min(start + Int(normalizedPageSize), posts.count)
    let slice = (start < posts.count) ? Array(posts[start..<end]) : []

    return PostPage(
      page: normalizedPage,
      page_size: normalizedPageSize,
      total_posts: UInt32(posts.count),
      posts: slice.map(makePreview)
    )
  }

  func getPost(slug: String) -> PostDetail {
    guard let post = posts.first(where: { $0.slug == slug }) else {
      let author = authorsBySlug["editorial-desk"] ?? AuthorPreview()
      return PostDetail(
        id: 0,
        slug: slug,
        title: "Post not found",
        summary: "No post matched the requested slug.",
        body_html: "<p>The requested post was not found in the example dataset.</p>",
        published_at: "",
        author: author,
        tags: ["missing"]
      )
    }

    return PostDetail(
      id: post.id,
      slug: post.slug,
      title: post.title,
      summary: post.summary,
      body_html: post.bodyHtml,
      published_at: post.publishedAt,
      author: author(for: post.authorSlug),
      tags: post.tags
    )
  }

  func listComments(postId: UInt64, page: UInt32, pageSize: UInt32) -> [Comment] {
    let allComments = commentsByPostId[postId] ?? []
    let normalizedPage = max(page, 1)
    let normalizedPageSize = max(pageSize, 1)
    let start = Int((normalizedPage - 1) * normalizedPageSize)
    let end = min(start + Int(normalizedPageSize), allComments.count)
    let slice = (start < allComments.count) ? Array(allComments[start..<end]) : []
    return slice.map { comment in
      Comment(
        id: comment.id,
        author_name: comment.authorName,
        body: comment.body,
        created_at: comment.createdAt
      )
    }
  }

  func listAuthorPosts(authorSlug: String, page: UInt32, pageSize: UInt32) -> [PostPreview] {
    let authorPosts = posts.filter { $0.authorSlug == authorSlug }
    let normalizedPage = max(page, 1)
    let normalizedPageSize = max(pageSize, 1)
    let start = Int((normalizedPage - 1) * normalizedPageSize)
    let end = min(start + Int(normalizedPageSize), authorPosts.count)
    let slice = (start < authorPosts.count) ? Array(authorPosts[start..<end]) : []
    return slice.map(makePreview)
  }

  func getAuthor(authorSlug: String) -> AuthorPreview {
    authorsBySlug[authorSlug] ?? AuthorPreview(
      id: 0,
      slug: authorSlug,
      name: "Unknown author",
      bio: "No author matched the requested slug.",
      avatar_url: ""
    )
  }

  private func author(for slug: String) -> AuthorPreview {
    getAuthor(authorSlug: slug)
  }

  private func makePreview(_ post: BlogPostRecord) -> PostPreview {
    PostPreview(
      id: post.id,
      slug: post.slug,
      title: post.title,
      excerpt: post.excerpt,
      published_at: post.publishedAt,
      cover_url: post.coverUrl,
      author: author(for: post.authorSlug)
    )
  }
}

private final class BlogServiceImpl: BlogServiceServant, @unchecked Sendable {
  private let repository: BlogRepository

  init(repository: BlogRepository) {
    self.repository = repository
    super.init()
  }

  override func listPosts(page: UInt32, page_size: UInt32) -> PostPage {
    repository.listPosts(page: page, pageSize: page_size)
  }

  override func getPost(slug: String) -> PostDetail {
    repository.getPost(slug: slug)
  }

  override func listComments(post_id: UInt64, page: UInt32, page_size: UInt32) -> [Comment] {
    repository.listComments(postId: post_id, page: page, pageSize: page_size)
  }

  override func listAuthorPosts(author_slug: String, page: UInt32, page_size: UInt32) -> [PostPreview] {
    repository.listAuthorPosts(authorSlug: author_slug, page: page, pageSize: page_size)
  }

  override func getAuthor(author_slug: String) -> AuthorPreview {
    repository.getAuthor(authorSlug: author_slug)
  }
}

private final class ChatServiceImpl: ChatServiceServant, @unchecked Sendable {
  override func joinPostChat(post_id: UInt64, user_name: String, stream: NPRPCBidiStream<ChatServerEvent, ChatEnvelope>) async {
    let joined = ChatServerEvent(
      message: ChatEnvelope(author: "system", body: "\(user_name) joined post #\(post_id).", created_at: "2026-03-09T09:00:00Z"),
      presence: PresenceEvent(user_name: user_name, kind: .joined)
    )
    stream.writer.write(joined)

    do {
      for try await incoming in stream.reader {
        let echoed = ChatServerEvent(
          message: ChatEnvelope(
            author: incoming.author.isEmpty ? user_name : incoming.author,
            body: incoming.body,
            created_at: incoming.created_at.isEmpty ? "2026-03-09T09:00:00Z" : incoming.created_at
          ),
          presence: PresenceEvent(user_name: "", kind: .typing)
        )
        stream.writer.write(echoed)
      }

      stream.writer.write(
        ChatServerEvent(
          message: ChatEnvelope(author: "system", body: "\(user_name) left the room.", created_at: "2026-03-09T09:01:00Z"),
          presence: PresenceEvent(user_name: user_name, kind: .left)
        )
      )
      stream.writer.close()
    } catch {
      stream.writer.abort()
    }
  }
}

private final class MediaServiceImpl: MediaServiceServant, @unchecked Sendable {
  override func openPostVideo(post_id: UInt64) -> AsyncStream<binary> {
    AsyncStream { continuation in
      continuation.yield(Array("placeholder-video-post-\(post_id)-chunk-1".utf8))
      continuation.yield(Array("placeholder-video-post-\(post_id)-chunk-2".utf8))
      continuation.finish()
    }
  }
}

private func buildIndexHtml(postCount: Int, hostJsonPath: String) -> String {
  """
  <!doctype html>
  <html>
    <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Live Blog Swift Backend</title>
    <style>
      body { font-family: system-ui, sans-serif; margin: 3rem auto; max-width: 62rem; padding: 0 1.5rem; color: #1c1917; background: #faf7f2; }
      code { background: #f5f5f4; padding: 0.15rem 0.35rem; border-radius: 0.35rem; }
      .card { border: 1px solid #e7e5e4; border-radius: 1rem; padding: 1.5rem; margin-top: 1rem; background: white; }
      .mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
    </style>
    </head>
    <body>
    <h1>Live Blog Swift Backend</h1>
    <p>This server now exposes typed NPRPC services for blog listing, post detail, author lookups, comments, a toy chat loopback stream, and a placeholder media stream.</p>
    <div class="card">
      <p><strong>Static root:</strong> <span class="mono">\(hostJsonPath.replacingOccurrences(of: "/host.json", with: ""))</span></p>
      <p><strong>Bootstrap:</strong> <a href="/host.json"><code>/host.json</code></a></p>
      <p><strong>Objects:</strong> <code>blog</code>, <code>chat</code>, <code>media</code></p>
      <p><strong>Mock posts available:</strong> \(postCount)</p>
    </div>
    </body>
  </html>
  """
}

print("╔══════════════════════════════════════════════════════════╗")
print("║                  Live Blog Swift Server                  ║")
print("╚══════════════════════════════════════════════════════════╝")
print()

do {
  let certFile = "/app/certs/out/localhost.crt"
  let keyFile = "/app/certs/out/localhost.key"
  let runtimeRoot = "/app/runtime-www"
  let staticRoot = runtimeRoot + "/client"
  let httpPort: UInt16 = 8443
  let repository = BlogRepository()

  try FileManager.default.createDirectory(atPath: runtimeRoot, withIntermediateDirectories: true)
  try FileManager.default.createDirectory(atPath: staticRoot, withIntermediateDirectories: true)

  let rpc = try RpcBuilder()
    .setLogLevel(.trace)
    .withHostname("localhost")
    .withHttp(httpPort)
      .ssl(certFile: certFile, keyFile: keyFile)
      .enableSsr(handlerDir: runtimeRoot)
      .rootDir(staticRoot)
      .enableHttp3()
    .build()

  let poa = try rpc.createPoa(maxObjects: 32, lifetime: .persistent, idPolicy: .systemGenerated)
  let browserFlags: ObjectActivationFlags = [.allowSecuredHttp, .allowSslWebSocket, .allowQuic]

  let blogObjectId = try poa.activateObject(BlogServiceImpl(repository: repository), flags: browserFlags)
  let chatObjectId = try poa.activateObject(ChatServiceImpl(), flags: browserFlags)
  let mediaObjectId = try poa.activateObject(MediaServiceImpl(), flags: browserFlags)

  rpc.clearHostJson()
  try rpc.addToHostJson(name: "blog", objectId: blogObjectId)
  try rpc.addToHostJson(name: "chat", objectId: chatObjectId)
  try rpc.addToHostJson(name: "media", objectId: mediaObjectId)
  let hostJsonPath = try rpc.produceHostJson()

  let indexHtml = buildIndexHtml(postCount: repository.posts.count, hostJsonPath: hostJsonPath)
  try indexHtml.write(toFile: staticRoot + "/index.html", atomically: true, encoding: .utf8)

  print("HTTP root: \(staticRoot)")
  print("SSR handler root: \(runtimeRoot)")
  print("HTTPS/WebTransport port: \(httpPort)")
  print("host.json: \(hostJsonPath)")
  print("Objects: blog, chat, media")
  print("Mock post count: \(repository.posts.count)")
  print()

  let signalSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
  signalSource.setEventHandler {
    print("\nReceived SIGINT, shutting down...")
    rpc.stop()
    exit(0)
  }
  signal(SIGINT, SIG_IGN)
  signalSource.resume()

  try rpc.startThreadPool(4)
  dispatchMain()
} catch {
  let standardError = FileHandle.standardError
  let message = "Fatal: \(error)\n"
  standardError.write(Data(message.utf8))
  exit(1)
}