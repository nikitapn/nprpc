// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.hpp"
#include "arguments_builder.hpp"
#include <functional>
#include <memory>

namespace npidl::builders {

template <typename Fn> struct OstreamWrapper {
  Fn fn;
};

template <typename Fn>
inline std::ostream& operator<<(std::ostream& os,
                                const OstreamWrapper<Fn>& wrapper)
{
  wrapper.fn(os);
  return os;
}

class BlockDepth
{
  friend std::ostream& operator<<(std::ostream&, const BlockDepth&);
  size_t depth_ = 0;

public:
  std::string str() const noexcept { return std::to_string(depth_); }

  BlockDepth& operator=(size_t n)
  {
    depth_ = n;
    return *this;
  }

  BlockDepth& operator--()
  {
    // assert(depth_);
    if (depth_ == 0)
      depth_ = 1;
    --depth_;
    return *this;
  }

  BlockDepth& operator++()
  {
    ++depth_;
    return *this;
  }

  BlockDepth operator++(int)
  {
    BlockDepth tmp = *this;
    ++depth_;
    return tmp;
  }

  BlockDepth operator+(size_t depth)
  {
    BlockDepth tmp = *this;
    tmp.depth_ += depth;
    return tmp;
  }

  BlockDepth operator-(size_t depth)
  {
    assert(depth_ > depth);
    BlockDepth tmp = *this;
    tmp.depth_ -= depth;
    return tmp;
  }

  BlockDepth& operator-=(size_t depth)
  {
    assert(depth_ > depth);
    depth_ -= depth;
    return *this;
  }

  BlockDepth& operator+=(size_t depth)
  {
    depth_ += depth;
    return *this;
  }
};

inline std::ostream& operator<<(std::ostream& os, const BlockDepth& block)
{
  for (size_t i = 0; i < block.depth_; ++i)
    os << "  ";
  return os;
}

class Builder
{
protected:
  // Argument for emint_struct2 to determine what kind of struct we're emitting
  // (regular struct, exception struct, or argument struct for proxy/servant)
  enum class Target { Regular, Exception, FunctionArgument };

  Context* ctx_;
  BlockDepth block_depth_;
  bool always_full_namespace_ = false;

  void always_full_namespace(bool flag) { always_full_namespace_ = flag; }

  /**
   * @brief Emit all cached argument structs via the provided callback.
   * 
   * This is used by language backends to generate marshalling structs
   * after all functions have been processed by ArgumentsStructBuilder.
   * 
   * @param emitter Function to call for each argument struct
   */
  void emit_arguments_structs(std::function<void(AstStructDecl*)> emitter);

  // Start a new block
  auto bb(bool newline = true)
  {
    return OstreamWrapper{[this, newline](std::ostream& os) {
      if (newline)
        os << block_depth_ << "{\n";
      ++block_depth_;
    }};
  }

  // End the current block
  auto eb(bool newline = true)
  {
    return OstreamWrapper{[this, newline](std::ostream& os) {
      --block_depth_;
      if (newline)
        os << block_depth_ << "}\n";
    }};
  }

  // Output current block depth
  auto bl()
  {
    return OstreamWrapper{[this](std::ostream& os) { os << block_depth_; }};
  }

public:
  virtual void emit_constant(const std::string& name, AstNumber* number) = 0;
  virtual void emit_struct(AstStructDecl* s) = 0;
  virtual void emit_exception(AstStructDecl* s) = 0;
  virtual void emit_namespace_begin() = 0;
  virtual void emit_namespace_end() = 0;
  virtual void emit_interface(AstInterfaceDecl* ifs) = 0;
  virtual void emit_using(AstAliasDecl* u) = 0;
  virtual void emit_enum(AstEnumDecl* e) = 0;
  /**
   * @brief Finalize the builder, write any pending data to files.
   */
  virtual void finalize() = 0;

  Builder(Context* ctx)
      : ctx_{ctx}
  {
  }
  virtual ~Builder() = default;

  virtual Builder* clone(Context* ctx) const = 0;
};

class BuildGroup
{
  Context* ctx_;
  std::vector<std::unique_ptr<Builder>> builders_;

  // Helper for generating argument structs
  ArgumentsStructBuilder args_builder_;

public:
  template <typename F, typename... Args> void emit(F fptr, Args&&... args)
  {
    auto mf = std::mem_fn(fptr);
    for (auto& ptr : builders_) {
      mf(ptr.get(), std::forward<Args>(args)...);
    }
  }

  template <typename T, typename... Args> void add(Args&&... args)
  {
    builders_.emplace_back(
        std::make_unique<T>(ctx_, std::forward<Args>(args)...));
  }

  void generate_argument_structs(AstInterfaceDecl* ifs);

  void finalize() { emit(&Builder::finalize); }

  BuildGroup(const BuildGroup& other, Context* ctx)
      : ctx_{ctx}, args_builder_(ctx)
  {
    for (auto& other_builder : other.builders_) {
      builders_.emplace_back(other_builder->clone(ctx));
    }
  }

  BuildGroup(Context* ctx = nullptr)
      : ctx_{ctx}, args_builder_(ctx)
  {
  }
};

} // namespace npidl::builders
