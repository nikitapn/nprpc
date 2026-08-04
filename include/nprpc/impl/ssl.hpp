// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/common.hpp>

#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace nprpc::impl {

using ssl_stream = beast::ssl_stream<beast_tcp_stream_strand>;
using ssl_ws = beast::websocket::stream<beast::ssl_stream<beast_tcp_stream_strand>>;

} // namespace nprpc::impl