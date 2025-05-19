// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "ast.hpp"
#include <functional>

int get_fundamental_size(TokenId token_id);
int align_offset(int align_of, int& last_field_ended, int size, int elements_size = 1);
void calc_struct_size_align(Ast_Struct_Decl* s);
std::tuple<int, int> get_type_size_align(Ast_Type_Decl* type);
bool is_flat(Ast_Type_Decl* type);
bool is_fundamental(Ast_Type_Decl* type);

constexpr uint32_t size_of_header       = 16;
constexpr uint32_t size_of_call_header  = 16;
constexpr uint32_t align_of_call_header = 8;

constexpr size_t size_of_object  = 48;
constexpr size_t align_of_object = 8;

inline constexpr uint32_t  get_arguments_offset() {
	static_assert(( size_of_header + align_of_call_header ) % 8 == 0);
	return size_of_header + size_of_call_header;
}

void dfs_interface(std::function<void(Ast_Interface_Decl*)> fn, Ast_Interface_Decl* start);

