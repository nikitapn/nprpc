// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <vector>

#include <nprpc_base_ext.hpp>
#include <nprpc/export.hpp>
#include <nprpc/task.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>

namespace nprpc {

struct SessionContext;
class StreamWriterBase;
class StreamReaderBase;

namespace impl {

class Session;

// Manages active streams per session
class NPRPC_API StreamManager : public std::enable_shared_from_this<StreamManager>
{
public:
  // Callback type for sending on main stream (control messages, always reliable)
  using SendCallback = std::function<void(flat_buffer&&)>;
  // Callback type for sending on native QUIC stream (reliable stream data)
  using SendNativeStreamCallback = std::function<void(flat_buffer&&)>;
  // Callback type for sending datagrams (unreliable stream data)
  using SendDatagramCallback = std::function<bool(flat_buffer&&)>;

  // Generate unique stream ID (client-side)
  static uint64_t generate_stream_id();

  StreamManager(SessionContext& session,
               std::shared_ptr<std::weak_ptr<Session>> self_cell,
               boost::asio::any_io_executor executor);
  ~StreamManager();

  // Set the callback for sending on main stream (control messages)
  void set_send_callback(SendCallback callback) { send_callback_ = std::move(callback); }

  // Set the callback for sending on native QUIC streams (reliable stream data)
  void set_send_native_stream_callback(SendNativeStreamCallback callback) { send_native_stream_callback_ = std::move(callback); }

  // Set the callback for sending datagrams (unreliable stream data)
  void set_send_datagram_callback(SendDatagramCallback callback) { send_datagram_callback_ = std::move(callback); }

  // Server-side: register outgoing stream (with optional unreliable flag)
  void register_stream(uint64_t stream_id,
                       std::unique_ptr<StreamWriterBase> writer,
                       bool unreliable = false);

  // Client-side: register incoming stream
  void register_reader(uint64_t stream_id, StreamReaderBase* reader);
  void register_reader(uint64_t stream_id, std::unique_ptr<StreamReaderBase> reader);
  void unregister_reader(uint64_t stream_id, StreamReaderBase* reader);
  void set_reader_unreliable(uint64_t stream_id, bool unreliable);

  // Handle incoming messages
  void on_chunk_received(flat_buffer&& fb);
  void on_stream_complete(uint64_t stream_id, uint64_t final_sequence);
  void on_stream_error(uint64_t stream_id, uint32_t error_code, flat_buffer&& error_data);
  void on_stream_cancel(uint64_t stream_id);
  // Flow control: credit grant from the remote consumer
  void on_window_update(uint64_t stream_id, uint32_t credits);

  // External writer interface (called from C bridge / Swift).
  // Registers a stream entry with the initial credit window so that
  // write_chunk_or_queue() can perform credit-based flow control.
  void register_external_writer(uint64_t stream_id);

  // Try to send a chunk for an external writer.  If credits are available
  // the chunk is sent synchronously and callback(success) is called before
  // return.  If no credits are available the data is queued and
  // callback(success) is called (on the stream executor) when credits are
  // granted via on_window_update(). success is false when the underlying
  // session has died (see send_chunk()) — the chunk was dropped, not sent.
  void write_chunk_or_queue(uint64_t stream_id,
                            std::span<const uint8_t> data,
                            uint64_t sequence,
                            std::function<void(bool)> callback);

  // Send methods. send_chunk() returns false (without throwing/aborting) if
  // the owning session has already died — the caller (ultimately the Swift
  // NPRPCStreamWriter) surfaces this as a thrown error rather than the
  // write silently vanishing.
  bool send_chunk(uint64_t stream_id,
                  std::span<const uint8_t> data,
                  uint64_t sequence);
  void send_complete(uint64_t stream_id, uint64_t final_sequence);
  void send_error(uint64_t stream_id,
                  uint32_t error_code,
                  std::span<const uint8_t> error_data);
  void send_cancel(uint64_t stream_id);
  // Flow control: grant credits to the remote producer
  void send_window_update(uint64_t stream_id, uint32_t credits);

  // Defer stream activation until after the current reply is queued.
  void defer_stream_start(uint64_t stream_id);
  void start_task_after_reply(uint64_t stream_id, ::nprpc::Task<> task);
  void on_reply_sent();

  // Cleanup
  void cancel_all();

  // External (non-C++-owner) shared ownership, for holders reachable only
  // through the opaque `void*` handle crossing the C bridge (the Swift
  // NPRPCStreamWriter/NPRPCStreamReader). Session owns this object via its
  // own shared_ptr (stream_manager_owner_) for as long as the session is
  // alive, but a Swift writer/reader can legitimately outlive the session
  // (e.g. it just hasn't noticed the disconnect yet) — external_retain()
  // takes this object's own share to keep it alive until the matching
  // external_release(), regardless of whether Session has already dropped
  // its own share. Without this, StreamManager either had to leak forever
  // (the previous behavior) or risk being freed out from under a Swift-side
  // raw pointer that has no way to know it's now dangling.
  void external_retain();
  void external_release();

  template <typename Fn>
  void post(Fn&& fn)
  {
    boost::asio::post(stream_executor_, [fn = std::forward<Fn>(fn)]() mutable { fn(); });
  }

private:
  SessionContext& session_;
  // Independent of session_'s own lifetime — kept alive by our own copy of
  // the shared_ptr, always safe to lock() even after the owning Session (and
  // session_) has been destroyed. See Session::bind_self() / self_cell_.
  std::shared_ptr<std::weak_ptr<Session>> self_cell_;
  boost::asio::any_io_executor stream_executor_;
  SendCallback send_callback_;
  SendNativeStreamCallback send_native_stream_callback_;
  SendDatagramCallback send_datagram_callback_;
  // One entry per outstanding external_retain(). Guarded by mutex_.
  std::vector<std::shared_ptr<StreamManager>> extra_owners_;

  // Active outgoing streams (server-side)
  static constexpr int32_t kInitialWindowSize = 8; // pre-grant before first update
  struct StreamInfo {
    std::unique_ptr<StreamWriterBase> writer;  // null for external (Swift) writers
    bool unreliable = false;
    int32_t credits = kInitialWindowSize;
    // Timer used to put the coroutine to sleep when credits are exhausted.
    // Owned by the coroutine stack; we store only a non-owning pointer here.
    // Guarded by mutex_.
    boost::asio::steady_timer* credit_timer = nullptr;
    // Queue for external (non-coroutine) writers.  When credits reach 0,
    // write requests are parked here and drained by on_window_update().
    struct PendingWrite {
      std::vector<uint8_t> data;
      uint64_t sequence;
      std::function<void(bool)> callback; // invoked when the chunk is sent (or dropped)
    };
    std::deque<PendingWrite> pending_writes;
  };
  std::unordered_map<uint64_t, StreamInfo> writers_;

  // Active incoming streams (client-side)
  struct ReaderState {
    StreamReaderBase* reader = nullptr;
    std::unique_ptr<StreamReaderBase> owned_reader;
    bool unreliable = false;
    std::optional<uint64_t> final_sequence;
    uint64_t next_expected_sequence = 0;
    std::map<uint64_t, flat_buffer> pending_chunks;
  };
  std::unordered_map<uint64_t, ReaderState> readers_;

  std::unordered_map<uint64_t, std::vector<flat_buffer>> pending_messages_;
  std::unordered_map<uint64_t, ::nprpc::Task<>> active_tasks_;
  std::vector<uint64_t> pending_stream_starts_;
  std::unordered_set<uint64_t> started_streams_;

  // Mutex for thread-safe access
  std::mutex mutex_;

  // Internal helper to determine if a stream is unreliable
  bool is_stream_unreliable(uint64_t stream_id) const;
  bool is_stream_started(uint64_t stream_id) const;
  // Returns false without dispatching if the owning session has died.
  bool dispatch_buffer(uint64_t stream_id, flat_buffer&& fb);
  void start_stream(uint64_t stream_id);
};

}} // namespace impl
