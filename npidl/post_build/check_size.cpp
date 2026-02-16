// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "utils.hpp"
#include <nprpc_base.hpp>

static_assert(sizeof(nprpc::impl::flat::CallHeader) == npidl::size_of_call_header);
static_assert(alignof(nprpc::impl::flat::CallHeader) == npidl::align_of_call_header);
static_assert(sizeof(nprpc::impl::flat::StreamInit) == npidl::size_of_stream_init_header);
static_assert(alignof(nprpc::impl::flat::StreamInit) == npidl::align_of_stream_init_header);
static_assert(sizeof(nprpc::detail::flat::ObjectId) == npidl::size_of_object);
static_assert(alignof(nprpc::detail::flat::ObjectId) == npidl::align_of_object);
