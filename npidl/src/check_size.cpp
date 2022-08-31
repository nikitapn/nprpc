// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

// #define BROKEN

#ifndef BROKEN
# include "../../include/nprpc/nprpc_base.hpp"
# include "utils.hpp"

static_assert(sizeof(nprpc::impl::flat::CallHeader) == size_of_call_header);
static_assert(alignof(nprpc::impl::flat::CallHeader) == align_of_call_header);

static_assert(sizeof(nprpc::detail::flat::ObjectId) == size_of_object);
static_assert(alignof(nprpc::detail::flat::ObjectId) == align_of_object);
static_assert(offsetof(nprpc::detail::flat::ObjectId, class_id) == size_of_object_without_class_id);

#endif
