// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/serialization/serialization.h>
#include <ostream>

namespace nprpc::serialization {
class binary_iarchive : public basic_iarchive<binary_iarchive>
{
public:
  template <typename U> void load_fundamental(U& obj)
  {
    is_.read(reinterpret_cast<char*>(&obj), sizeof(U));
  }

  void load_byte_sequence(char* ptr, unsigned int size) { is_.read(ptr, size); }

  binary_iarchive(std::istream& is)
      : basic_iarchive<binary_iarchive>(is)
  {
  }
};

class text_iarchive : public basic_iarchive<text_iarchive>
{
public:
  template <typename U> void load_fundamental(U& obj) { is_ >> obj; }

  void load_byte_sequence(char* ptr, unsigned int size) { is_.read(ptr, size); }

  text_iarchive(std::istream& is)
      : basic_iarchive<text_iarchive>(is)
  {
  }
};

} // namespace nprpc::serialization
