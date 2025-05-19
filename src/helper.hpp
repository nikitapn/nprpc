// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <boost/asio.hpp>
#include <nprpc/endpoint.hpp>


namespace nprpc::impl {

boost::asio::ip::tcp::endpoint
sync_socket_connect(const EndPoint& endpoint, boost::asio::ip::tcp::socket& socket);

}  // namespace nprpc::impl