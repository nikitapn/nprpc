// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio.hpp>
#include <nprpc/endpoint.hpp>

namespace nprpc::impl {

boost::asio::ip::tcp::endpoint
sync_socket_connect(const EndPoint& endpoint,
                    boost::asio::ip::tcp::socket& socket);

} // namespace nprpc::impl