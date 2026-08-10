// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

// Shared default values for NPRPC configuration structures.
// Keep these macros in sync across the core C++ API and the Swift bridge.

#define NPRPC_DEFAULT_LOG_LEVEL_U32 3
#define NPRPC_DEFAULT_HTTP_MAX_REQUEST_BODY_SIZE 10'000
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_COMPRESSION_ENABLED true
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_MAX_MESSAGE_SIZE (2 * 1024 * 1024)
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_MAX_MESSAGE_SIZE (2 * 1024 * 1024)
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_MAX_ACTIVE_SESSIONS_PER_IP 0
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_UPGRADES_PER_IP_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_UPGRADES_BURST 0
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_REQUESTS_PER_SESSION_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_WEBSOCKET_REQUESTS_BURST 0
#define NPRPC_DEFAULT_HTTP3_WORKER_COUNT 4
#define NPRPC_DEFAULT_HTTP3_MAX_ACTIVE_CONNECTIONS_PER_IP 0
#define NPRPC_DEFAULT_HTTP3_MAX_NEW_CONNECTIONS_PER_IP_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP3_MAX_NEW_CONNECTIONS_BURST 0
#define NPRPC_DEFAULT_HTTP_RPC_MAX_REQUESTS_PER_IP_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_RPC_MAX_REQUESTS_BURST 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_CONNECTS_PER_IP_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_CONNECTS_BURST 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_REQUESTS_PER_SESSION_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_REQUESTS_BURST 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_STREAM_OPENS_PER_SESSION_PER_SECOND 0
#define NPRPC_DEFAULT_HTTP_WEBTRANSPORT_STREAM_OPENS_BURST 0
#define NPRPC_DEFAULT_WATCH_FILES false

// Shared-memory transport.  Every accepted client costs two rings of
// NPRPC_DEFAULT_SHM_RING_BUFFER_SIZE, and they are resident from the moment
// they are created rather than faulted in on use — so this number multiplies
// by the number of connected clients whether they say anything or not.
//
// 1 MiB suits what actually crosses a control plane: calls, replies and
// stream frames measured in tens or hundreds of bytes.  A server that ships
// whole images or documents through the ring wants a bigger one, and should
// say so with RpcBuilderBase::shm_channel_sizes rather than have everybody
// pay for it.
#define NPRPC_DEFAULT_SHM_RING_BUFFER_SIZE (1024 * 1024)
// Half a ring, so a full-sized message never has to wait for the ring to
// drain completely before it can be claimed.  Must stay below the ring size:
// a message the ring cannot hold can never be sent (see validation in
// SharedMemoryChannel).
#define NPRPC_DEFAULT_SHM_MAX_MESSAGE_SIZE (512 * 1024)
