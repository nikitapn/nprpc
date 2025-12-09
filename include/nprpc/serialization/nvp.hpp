// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

template <size_t N> class static_string
{
  const char data_[N + 1];

  template <std::size_t... I>
  constexpr static_string(const char (&data)[N + 1], std::index_sequence<I...>)
      : data_{data[I]...}
  {
  }

public:
  constexpr char operator[](std::size_t i) const { return data_[i]; }

  constexpr auto cv() const { return std::string_view(data_, N); }

  constexpr size_t size() const { return N; }

  explicit constexpr static_string(const char (&data)[N + 1])
      : static_string(data, std::make_index_sequence<N + 1>())
  {
  }
};

struct nvp_base {
};
template <size_t N, typename T> struct nvp : nvp_base {
  const static_string<N> name;
  T& obj;

  explicit constexpr nvp(const char (&_name)[N + 1], T& _obj)
      : name(_name), obj(_obj)
  {
  }
};

template <size_t N, typename T>
inline constexpr auto make_nvp_impl(const char (&lit)[N], T& obj)
    -> nvp<N - 1, T>
{
  return nvp<N - 1, T>(lit, obj);
}

#define NVP(x) make_nvp_impl(#x, x)
#define NVP2(x, y) make_nvp_impl(x, y)
