// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#ifndef NPRPC_HTTP_UTILS_HPP
#define NPRPC_HTTP_UTILS_HPP

#include <string>
#include <string_view>

namespace nprpc::impl {

/// Return a reasonable MIME type based on the file extension.
/// The returned string_view points to static storage and is always valid.
std::string_view mime_type(std::string_view path);

/// Append an HTTP relative path to a local filesystem path.
/// The returned path is normalized for the platform.
std::string path_cat(std::string_view base, std::string_view path);

} // namespace nprpc::impl

#endif // NPRPC_HTTP_UTILS_HPP
