// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "ast.hpp"

namespace npidl::builders {

/**
 * @brief Standalone builder for function argument structures.
 * 
 * This builder creates marshalling structs for function arguments,
 * independent of any specific language backend (C++, Swift, TypeScript).
 * It populates the function's in_s and out_s fields and stores unique
 * argument structs in ctx_->affa_list.
 */
class ArgumentsStructBuilder
{
  Context* ctx_;

public:
  explicit ArgumentsStructBuilder(Context* ctx)
      : ctx_(ctx)
  {
  }

  /**
   * @brief Generate input/output argument structs for a function.
   * 
   * This creates two structs (if needed):
   * - in_s: Contains all 'in' parameters
   * - out_s: Contains all 'out' parameters + return value (if not void/stream)
   * 
   * Structs are cached in ctx_->affa_list by their canonical ID to avoid
   * duplicates across different functions with identical signatures.
   * 
   * @param fn The function declaration to process
   */
  void make_arguments_structs(AstFunctionDecl* fn);

private:
  AstStructDecl* make_struct(AstFunctionDecl* fn, 
                             std::vector<AstFunctionArgument*>& args);
};

} // namespace npidl::builders
