#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/signal_set.hpp>

#include <nprpc/nprpc.hpp>

#include "live_blog.hpp"

#ifndef NPRPC_LIVE_BLOG_STATIC_ROOT
#  define NPRPC_LIVE_BLOG_STATIC_ROOT ""
#endif

#ifndef NPRPC_LIVE_BLOG_RUNTIME_ROOT
#  define NPRPC_LIVE_BLOG_RUNTIME_ROOT ""
#endif

#ifndef NPRPC_LIVE_BLOG_MEDIA_ROOT
#  define NPRPC_LIVE_BLOG_MEDIA_ROOT ""
#endif

#ifndef NPRPC_LIVE_BLOG_CERT_FILE
#  define NPRPC_LIVE_BLOG_CERT_FILE ""
#endif

#ifndef NPRPC_LIVE_BLOG_KEY_FILE
#  define NPRPC_LIVE_BLOG_KEY_FILE ""
#endif

namespace {

using live_blog::AuthorPreview;
using live_blog::ChatEnvelope;
using live_blog::ChatServerEvent;
using live_blog::Comment;
using live_blog::PostDetail;
using live_blog::PostPage;
using live_blog::PostPreview;
using live_blog::PresenceEvent;
using live_blog::PresenceEventKind;
using live_blog::binary;

std::string_view to_string_view(nprpc::flat::Span<char> value)
{
  return {static_cast<const char*>(value.data()), value.size()};
}

struct PostRecord {
  uint64_t id;
  std::string slug;
  std::string title;
  std::string excerpt;
  std::string summary;
  std::string body_html;
  std::string published_at;
  std::optional<std::string> cover_url;
  std::string author_slug;
  std::vector<std::string> tags;
};

class BlogRepository {
public:
  BlogRepository()
  {
    authors_.emplace(
        "editorial-desk",
        AuthorPreview{1,
                      "editorial-desk",
                      "Editorial Desk",
                      "The in-house team shaping the demo newsroom and keeping the NPRPC examples aligned across languages.",
                      "https://images.unsplash.com/photo-1494790108377-be9c29b29330?auto=format&fit=crop&w=400&q=80"});
    authors_.emplace(
        "nikita-lab",
        AuthorPreview{2,
                      "nikita-lab",
                      "Nikita Lab",
                      "Prototype-heavy notes about transports, codegen, and practical SSR boundaries.",
                      "https://images.unsplash.com/photo-1500648767791-00dcc994a43e?auto=format&fit=crop&w=400&q=80"});

    posts_ = {
        {101,
         "first-steps-with-nprpc",
         "First Steps With NPRPC",
         "A pragmatic walkthrough of typed contracts, browser bootstrap, and why the shell stays server-rendered.",
         "The shortest path from a route-aware shell to real typed RPC calls.",
         "<p>NPRPC lets the server render the route-aware frame first, then hydrate with typed RPC objects in the browser.</p><p>This merged example keeps one Svelte app and proves the backend can be implemented in either Swift or C++.</p>",
         "2026-03-09T09:00:00Z",
         std::optional<std::string>{"https://images.unsplash.com/photo-1516321318423-f06f85e504b3?auto=format&fit=crop&w=1200&q=80"},
         "editorial-desk",
         {"architecture", "intro", "rpc"}},
        {102,
         "swift-service-boundaries",
         "Swift Service Boundaries",
         "A look at why the example keeps business logic on the backend while Svelte owns only the shell and interaction polish.",
         "Keeping route shells and business logic separate makes transport changes much easier.",
         "<p>The same Svelte client can talk to multiple server implementations as long as the typed contracts stay stable.</p><p>That is the point of this unified example.</p>",
         "2026-03-08T12:30:00Z",
         std::nullopt,
         "nikita-lab",
         {"swift", "server", "contracts"}},
        {103,
         "cpp-and-svelte-together",
         "C++ And Svelte Together",
         "Why a C++ server is still a good fit for SSR hosting, object bootstrap, and transport-heavy workloads.",
         "C++ can host the same Svelte output and the same live-blog contracts.",
         "<p>The older SSR demo lived in its own Svelte app. This merged version removes that duplication.</p><p>The client stays the same; only the backend changes.</p>",
         "2026-03-07T18:45:00Z",
         std::nullopt,
         "editorial-desk",
         {"cpp", "ssr", "svelte"}},
        {104,
         "chat-stream-notes",
         "Chat Stream Notes",
         "A small bidi stream demo used by the post page so the browser can send envelopes and receive server events.",
         "The chat section is intentionally simple, but it exercises bidi streaming end to end.",
         "<p>The C++ demo backend echoes messages and emits presence events so the page can prove the transport story without extra infrastructure.</p>",
         "2026-03-06T14:10:00Z",
         std::nullopt,
         "nikita-lab",
         {"streaming", "chat", "demo"}},
        {105,
         "route-aware-shells",
         "Route-Aware Shells",
         "Why SSR still matters even when the real data comes later from RPC calls.",
         "SSR gives fast first paint and route framing without moving business logic into Node.",
         "<p>The shell already knows whether the user requested a blog page, author page, or post page.</p><p>That is enough to render the right structure before any RPC call completes.</p>",
         "2026-03-05T10:20:00Z",
         std::nullopt,
         "editorial-desk",
         {"ssr", "routing"}},
        {106,
         "one-client-two-backends",
         "One Client, Two Backends",
         "The same generated TypeScript contracts can target the Swift or C++ live-blog server variants.",
         "Shared IDL is the reason the merged example stays coherent.",
         "<p>Typed contracts are the seam that makes a multi-language demo maintainable.</p>",
         "2026-03-04T08:15:00Z",
         std::nullopt,
         "nikita-lab",
         {"idl", "typescript", "cpp", "swift"}},
    };

    comments_.emplace(101,
                      std::vector<Comment>{{1001, "Ada", "The single-client story is much easier to explain than two separate Svelte apps.", "2026-03-09T10:00:00Z"},
                                           {1002, "Linus", "The C++ backend variant is exactly the sort of transport-heavy demo I wanted to see.", "2026-03-09T10:05:00Z"}});
    comments_.emplace(103,
                      std::vector<Comment>{{1003, "Taylor", "Keeping SSR hosting in C++ while the UI stays in Svelte is a good split.", "2026-03-08T09:00:00Z"}});
  }

  PostPage list_posts(uint32_t page, uint32_t page_size) const
  {
    PostPage result;
    result.page = page;
    result.page_size = page_size;
    result.total_posts = static_cast<uint32_t>(posts_.size());

    if (page_size == 0) {
      return result;
    }

    const auto start = static_cast<size_t>((page > 0 ? page - 1 : 0) * page_size);
    if (start >= posts_.size()) {
      return result;
    }

    const auto end = std::min(posts_.size(), start + page_size);
    for (size_t index = start; index < end; ++index) {
      result.posts.push_back(make_preview(posts_[index]));
    }
    return result;
  }

  PostDetail get_post(std::string_view slug) const
  {
    const auto* post = find_post(slug);
    if (!post) {
      throw nprpc::Exception("Post not found");
    }

    PostDetail result;
    result.id = post->id;
    result.slug = post->slug;
    result.title = post->title;
    result.summary = post->summary;
    result.body_html = post->body_html;
    result.published_at = post->published_at;
    result.author = get_author(post->author_slug);
    result.tags = post->tags;
    return result;
  }

  std::vector<Comment> list_comments(uint64_t post_id, uint32_t page, uint32_t page_size) const
  {
    auto it = comments_.find(post_id);
    if (it == comments_.end() || page_size == 0) {
      return {};
    }

    const auto& items = it->second;
    const auto start = static_cast<size_t>((page > 0 ? page - 1 : 0) * page_size);
    if (start >= items.size()) {
      return {};
    }

    const auto end = std::min(items.size(), start + page_size);
    return std::vector<Comment>(items.begin() + static_cast<std::ptrdiff_t>(start),
                                items.begin() + static_cast<std::ptrdiff_t>(end));
  }

  std::vector<PostPreview> list_author_posts(std::string_view author_slug,
                                             uint32_t page,
                                             uint32_t page_size) const
  {
    std::vector<PostPreview> filtered;
    for (const auto& post : posts_) {
      if (post.author_slug == author_slug) {
        filtered.push_back(make_preview(post));
      }
    }

    if (page_size == 0) {
      return {};
    }

    const auto start = static_cast<size_t>((page > 0 ? page - 1 : 0) * page_size);
    if (start >= filtered.size()) {
      return {};
    }

    const auto end = std::min(filtered.size(), start + page_size);
    return std::vector<PostPreview>(filtered.begin() + static_cast<std::ptrdiff_t>(start),
                                    filtered.begin() + static_cast<std::ptrdiff_t>(end));
  }

  AuthorPreview get_author(std::string_view slug) const
  {
    auto it = authors_.find(std::string(slug));
    if (it == authors_.end()) {
      throw nprpc::Exception("Author not found");
    }
    return it->second;
  }

  std::size_t post_count() const noexcept { return posts_.size(); }

private:
  std::unordered_map<std::string, AuthorPreview> authors_;
  std::vector<PostRecord> posts_;
  std::unordered_map<uint64_t, std::vector<Comment>> comments_;

  const PostRecord* find_post(std::string_view slug) const
  {
    auto it = std::find_if(posts_.begin(), posts_.end(), [slug](const PostRecord& post) {
      return post.slug == slug;
    });
    return it == posts_.end() ? nullptr : &*it;
  }

  PostPreview make_preview(const PostRecord& post) const
  {
    PostPreview preview;
    preview.id = post.id;
    preview.slug = post.slug;
    preview.title = post.title;
    preview.excerpt = post.excerpt;
    preview.published_at = post.published_at;
    preview.cover_url = post.cover_url;
    preview.author = get_author(post.author_slug);
    return preview;
  }
};

ChatServerEvent make_chat_event(std::string user_name,
                                PresenceEventKind kind,
                                std::string author,
                                std::string body,
                                std::string created_at)
{
  ChatServerEvent event;
  event.message.author = std::move(author);
  event.message.body = std::move(body);
  event.message.created_at = std::move(created_at);
  event.presence.user_name = std::move(user_name);
  event.presence.kind = kind;
  return event;
}

class BlogServiceImpl final : public live_blog::IBlogService_Servant {
public:
  explicit BlogServiceImpl(const BlogRepository& repository)
      : repository_(repository)
  {
  }

  PostPage ListPosts(uint32_t page, uint32_t page_size) override
  {
    return repository_.list_posts(page, page_size);
  }

  PostDetail GetPost(nprpc::flat::Span<char> slug) override
  {
    return repository_.get_post(to_string_view(slug));
  }

  std::vector<Comment> ListComments(uint64_t post_id, uint32_t page, uint32_t page_size) override
  {
    return repository_.list_comments(post_id, page, page_size);
  }

  std::vector<PostPreview> ListAuthorPosts(nprpc::flat::Span<char> author_slug,
                                           uint32_t page,
                                           uint32_t page_size) override
  {
    return repository_.list_author_posts(to_string_view(author_slug),
                                         page,
                                         page_size);
  }

  AuthorPreview GetAuthor(nprpc::flat::Span<char> author_slug) override
  {
    return repository_.get_author(to_string_view(author_slug));
  }

private:
  const BlogRepository& repository_;
};

class ChatHub {
public:
  struct Participant {
    explicit Participant(nprpc::StreamWriter<ChatServerEvent>&& stream_writer)
        : writer(std::move(stream_writer)) {}

    void send(const ChatServerEvent& event) {
      std::lock_guard lock(write_mutex);
      if (!active) return;
      writer.write(event);
    }

    void close() {
      std::lock_guard lock(write_mutex);
      if (!active) return;
      active = false;
      writer.close();
    }

    void abort() {
      std::lock_guard lock(write_mutex);
      if (!active) return;
      active = false;
      writer.abort();
    }

    std::mutex write_mutex;
    bool active = true;
    nprpc::StreamWriter<ChatServerEvent> writer;
  };

  using ParticipantPtr = std::shared_ptr<Participant>;

  std::pair<uint64_t, ParticipantPtr> join(
      uint64_t post_id,
      nprpc::StreamWriter<ChatServerEvent>&& writer)
  {
    auto participant = std::make_shared<Participant>(std::move(writer));

    std::lock_guard lock(rooms_mutex_);
    const auto participant_id = next_participant_id_++;
    rooms_[post_id][participant_id] = participant;
    return {participant_id, std::move(participant)};
  }

  void leave(uint64_t post_id, uint64_t participant_id) {
    std::lock_guard lock(rooms_mutex_);
    auto room_it = rooms_.find(post_id);
    if (room_it == rooms_.end()) return;

    room_it->second.erase(participant_id);
    if (room_it->second.empty()) {
      rooms_.erase(room_it);
    }
  }

  void broadcast(uint64_t post_id, const ChatServerEvent& event) {
    std::vector<ParticipantPtr> recipients;

    {
      std::lock_guard lock(rooms_mutex_);
      auto room_it = rooms_.find(post_id);
      if (room_it == rooms_.end()) return;

      recipients.reserve(room_it->second.size());
      for (const auto& [_, participant] : room_it->second) {
        recipients.push_back(participant);
      }
    }

    for (const auto& participant : recipients) {
      participant->send(event);
    }
  }

private:
  std::mutex rooms_mutex_;
  uint64_t next_participant_id_ = 1;
  std::unordered_map<uint64_t, std::unordered_map<uint64_t, ParticipantPtr>> rooms_;
};

class ChatServiceImpl final : public live_blog::IChatService_Servant {
  ChatHub chat_hub_;
public:  nprpc::Task<> JoinPostChat(uint64_t post_id,
                             std::string user_name,
                             nprpc::BidiStream<ChatEnvelope, ChatServerEvent> stream) override
  {
    auto [participant_id, participant] = chat_hub_.join(post_id, std::move(stream.writer));
    try {
      chat_hub_.broadcast(post_id, make_chat_event(user_name,
                                                  PresenceEventKind::Joined,
                                                  "system",
                                                  user_name + " joined post #" + std::to_string(post_id),
                                                  "2026-03-09T09:00:00Z"));
      while (auto incoming = co_await stream.reader) {
        auto echoed = make_chat_event(user_name,
                                      PresenceEventKind::Typing,
                                      incoming->author.empty() ? user_name : incoming->author,
                                      incoming->body,
                                      incoming->created_at.empty() ? "2026-03-09T09:01:00Z" : incoming->created_at);
        chat_hub_.broadcast(post_id, echoed);
      }

      chat_hub_.broadcast(post_id, make_chat_event(user_name,
                                                  PresenceEventKind::Left,
                                                  "system",
                                                  user_name + " left post #" + std::to_string(post_id),
                                                  "2026-03-09T09:02:00Z"));
      chat_hub_.leave(post_id, participant_id);
    } catch (...) {
      participant->abort();
      throw;
    }

    co_return;
  }
};

class MediaServiceImpl final : public live_blog::IMediaService_Servant {
  // Directory holding fragmented MP4 files (post-<id>.fmp4) and DASH
  // manifests (post-<id>.mpd).  See Swift counterpart for generation notes.
  std::string media_dir_;

  // Stream in 4 MB chunks — same as the Swift implementation.
  static constexpr std::size_t kChunkSize = 4 * 1024 * 1024;

  // Resolves the actual DASH media file named by the MPD's <BaseURL> element.
  // Falls back to the raw fmp4 if the MPD is absent or has no <BaseURL>.
  std::string dash_media_path(uint64_t post_id) const
  {
    const auto mpd_path = media_dir_ + "/post-" + std::to_string(post_id) + ".mpd";
    std::ifstream mpd_file(mpd_path);
    if (mpd_file) {
      const std::string mpd((std::istreambuf_iterator<char>(mpd_file)),
                             std::istreambuf_iterator<char>());
      const auto tag_start = mpd.find("<BaseURL>");
      if (tag_start != std::string::npos) {
        const auto content_start = tag_start + 9; // len("<BaseURL>")
        const auto tag_end = mpd.find("</BaseURL>", content_start);
        if (tag_end != std::string::npos) {
          std::string filename = mpd.substr(content_start, tag_end - content_start);
          // Trim whitespace
          filename.erase(0, filename.find_first_not_of(" \t\r\n"));
          filename.erase(filename.find_last_not_of(" \t\r\n") + 1);
          if (!filename.empty() && filename.rfind("http", 0) != 0) {
            return media_dir_ + "/" + filename;
          }
        }
      }
    }
    return media_dir_ + "/post-" + std::to_string(post_id) + ".fmp4";
  }

public:
  explicit MediaServiceImpl(std::string media_dir)
      : media_dir_(std::move(media_dir))
  {
  }

  // Streams the raw fmp4 file in kChunkSize chunks.
  nprpc::StreamWriter<binary> OpenPostVideo(uint64_t post_id) override
  {
    const auto path = media_dir_ + "/post-" + std::to_string(post_id) + ".fmp4";
    std::ifstream file(path, std::ios::binary);
    if (!file) co_return;
    std::vector<uint8_t> buf(kChunkSize);
    while (file) {
      file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(kChunkSize));
      const auto n = static_cast<std::size_t>(file.gcount());
      if (n == 0) break;
      co_yield binary(buf.data(), buf.data() + n);
    }
  }

  std::string GetVideoDashManifest(uint64_t post_id) override
  {
    const auto path = media_dir_ + "/post-" + std::to_string(post_id) + ".mpd";
    std::ifstream file(path);
    if (!file) return {};
    return std::string((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  }

  // Streams a byte range from the single-file DASH media.
  // Shaka Player issues these for each segment after reading the sidx box.
  nprpc::StreamWriter<binary> GetVideoDashSegmentRange(
      uint64_t post_id, uint64_t byte_offset, uint64_t byte_length) override
  {
    const auto path = dash_media_path(post_id);
    std::ifstream file(path, std::ios::binary);
    if (!file) co_return;
    file.seekg(static_cast<std::streamoff>(byte_offset));
    if (!file) co_return;
    std::vector<uint8_t> buf(kChunkSize);
    uint64_t remaining = byte_length;
    while (remaining > 0 && file) {
      const auto to_read = static_cast<std::streamsize>(std::min(remaining, static_cast<uint64_t>(kChunkSize)));
      file.read(reinterpret_cast<char*>(buf.data()), to_read);
      const auto n = static_cast<std::size_t>(file.gcount());
      if (n == 0) break;
      co_yield binary(buf.data(), buf.data() + n);
      remaining -= n;
    }
  }
};

} // namespace

int main()
{
  try {
    const std::filesystem::path static_root = NPRPC_LIVE_BLOG_STATIC_ROOT;
    const std::filesystem::path runtime_root = NPRPC_LIVE_BLOG_RUNTIME_ROOT;
    const std::string media_dir = NPRPC_LIVE_BLOG_MEDIA_ROOT;

    std::filesystem::create_directories(runtime_root);
    std::filesystem::create_directories(static_root);
    std::filesystem::create_directories(media_dir);

    auto rpc = nprpc::RpcBuilder()
                   .set_log_level(nprpc::LogLevel::trace)
                   .with_hostname("localhost")
                   .with_http(8443)
                     .root_dir(static_root.string())
                     .ssl(NPRPC_LIVE_BLOG_CERT_FILE, NPRPC_LIVE_BLOG_KEY_FILE)
                     .enable_http3()
                     .enable_ssr(runtime_root.string())
                     .watch_files()
                   .build();

    BlogRepository repository;
    auto* poa = rpc->create_poa().with_max_objects(32).with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent).build();
    const auto browser_flags = nprpc::ObjectActivationFlags::https | nprpc::ObjectActivationFlags::wss;

    auto blog_id  = poa->activate_object(new BlogServiceImpl(repository), browser_flags);
    auto chat_id  = poa->activate_object(new ChatServiceImpl(), browser_flags);
    auto media_id = poa->activate_object(new MediaServiceImpl(media_dir), browser_flags);

    rpc->clear_host_json();
    rpc->add_to_host_json("blog",  blog_id);
    rpc->add_to_host_json("chat",  chat_id);
    rpc->add_to_host_json("media", media_id);
    const auto host_json_path = rpc->produce_host_json();

    boost::asio::signal_set signals(rpc->ioc(), SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) { rpc->ioc().stop(); });

    std::cout << "Starting live-blog C++ server on https://localhost:8443\n";
    std::cout << "Static root:      " << static_root << "\n";
    std::cout << "SSR handler root: " << runtime_root << "\n";
    std::cout << "Media dir:        " << media_dir << "  (place post-<id>.fmp4 files here)\n";
    std::cout << "host.json:        " << host_json_path << "\n";
    std::cout << "Objects:          blog, chat, media\n";
    std::cout << "Mock post count:  " << repository.post_count() << std::endl;

    rpc->run();
    rpc->destroy();
  } catch (const std::exception& ex) {
    std::cerr << "Error starting live-blog C++ server: " << ex.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}