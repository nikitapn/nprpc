// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <stdexcept>

namespace nprpc {
class Exception : public std::runtime_error {
public:
	explicit Exception(char const* const msg) noexcept
		: std::runtime_error(msg) {}
	
	explicit Exception(std::string const& msg) noexcept
		: std::runtime_error(msg) {}
};
}