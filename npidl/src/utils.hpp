#pragma once

#include "ast.hpp"

int get_fundamental_size(TokenId token_id);
int align_offset(int align_of, int& last_field_ended, int size, int elements_size = 1);
void calc_struct_size_align(Ast_Struct_Decl* s);
std::tuple<int, int> get_type_size_align(Ast_Type_Decl* type);
struct_id_t get_function_struct_id(const Ast_Struct_Decl* s);
bool is_flat(Ast_Type_Decl* type);

constexpr uint32_t size_of_call_header = 16;
constexpr uint32_t align_of_call_header = 8;

constexpr size_t size_of_object = 32;
constexpr size_t align_of_object = 8;
constexpr size_t size_of_object_without_class_id = 20;


inline constexpr uint32_t  get_arguments_offset() {
	// size of the message + MessageId + CallHeader + no need for padding
	return 4 + 4 + size_of_call_header;
}

