// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <glaze/glaze.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace npdoc {

// Inputs are other tools' formats that grow fields over time; only the ones
// npdoc models are read.
inline constexpr glz::opts read_opts{.error_on_unknown_keys = false};

template <typename T>
void read_json_file(const std::filesystem::path& path, T& value)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
    throw std::runtime_error("cannot open " + path.string());
  std::stringstream buf;
  buf << in.rdbuf();
  const auto text = buf.str();
  if (auto ec = glz::read<read_opts>(value, text))
    throw std::runtime_error(path.string() + ": " +
                             glz::format_error(ec, text));
}

} // namespace npdoc
