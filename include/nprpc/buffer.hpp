// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <array>
#include <boost/beast/core/flat_buffer.hpp>

namespace nprpc {
class Buffers {
	std::array<boost::beast::flat_buffer, 2> bufs_;
	size_t ix_ = 0;
public:
	boost::beast::flat_buffer& flip() noexcept {
		ix_ ^= 1;
		return bufs_[ix_];
	}

	boost::beast::flat_buffer& operator()() noexcept {
		return bufs_[ix_];
	}

	boost::beast::flat_buffer&& operator()(bool) noexcept {
		return std::move(bufs_[ix_]);
	}

	Buffers(size_t initial_capacity = 2 * 1024) {
		bufs_[0].prepare(initial_capacity);
		bufs_[1].prepare(initial_capacity);
	}
};
}