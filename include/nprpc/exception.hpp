// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <stdexcept>

namespace nprpc {
class Exception : public std::runtime_error {
public:
	explicit Exception(char const* const msg) noexcept
		: std::runtime_error(msg) {}
};

class ExceptionCommFailure : public Exception {
public:
	ExceptionCommFailure() noexcept
		: Exception("Communication error") {}
};

class ExceptionTimeout : public Exception {
public:
	ExceptionTimeout() noexcept
		: Exception("Timeout") {}
};

class ExceptionObjectNotExist : public Exception {
public:
	ExceptionObjectNotExist() noexcept
		: Exception("Object does not exist") {}
};

class ExceptionUnknownFunctionIndex : public Exception {
public:
	ExceptionUnknownFunctionIndex() noexcept
		: Exception("Unknown function index") {}
};
}