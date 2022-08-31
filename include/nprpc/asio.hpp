// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <iostream>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace net = boost::asio;
using net::ip::tcp;
using net::io_context;

static constexpr uint32_t max_message_size = 1024 * 1024 * 1024;

inline void fail(const boost::system::error_code& ec, const char* what) {
  std::cerr << what << ": " << ec.message() << '\n';
}

