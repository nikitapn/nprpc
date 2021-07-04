// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "utils.hpp"
#include <cassert>


int get_fundamental_size(TokenId token_id) {
	switch (token_id) {
	case TokenId::Boolean: case TokenId::Int8: case TokenId::UInt8:			return 1;
	case TokenId::Int16:	case TokenId::UInt16:													return 2;
	case TokenId::Int32:	case TokenId::UInt32: case TokenId::Float32:	return 4;
	case TokenId::Int64:	case TokenId::UInt64: case TokenId::Float64:	return 8;
	default: assert(false); return 0;
	}
}

int align_offset(int align_of, int& last_field_ended, int size, int elements_size) {
	int unalign = last_field_ended % align_of;
	int offset = last_field_ended + (unalign ? align_of - unalign : 0);
	last_field_ended = offset + (size * elements_size);
	return offset;
}

void calc_struct_size_align(Ast_Struct_Decl* s, Ast_Type_Decl* t, int& offset, int elements_size) {
	switch (t->id) {
		case FieldType::Fundamental: {
			int size = get_fundamental_size(cft(t)->token_id);
			if (s->align < size) s->align = size;
			align_offset(size, offset, size, elements_size);
			break;
		}
		case FieldType::Struct:
		{
			auto s = cflat(t);
			assert(s->align != -1 && s->size != -1);
			if (s->align < s->align) s->align = s->align;
			align_offset(s->align, offset, s->size, elements_size);
			break;
		}
		case FieldType::Array:
			calc_struct_size_align(s, car(t)->type, offset, car(t)->length);
			break;
		case FieldType::Alias:
			calc_struct_size_align(s, calias(t)->type, offset, 1);
			break;
		case FieldType::Enum: {
			int size = get_fundamental_size(cenum(t)->token_id);
			if (s->align < size) s->align = size;
			align_offset(size, offset, size, elements_size);
			break;
		}
		case FieldType::Vector:
		case FieldType::String:
			if (s->align < 4) s->align = 4;
			align_offset(4, offset, 8, elements_size);
			break;
		case FieldType::Object: // nprpc::detail::flat::ObjectId
			if (s->align < align_of_object) s->align = align_of_object;
			align_offset(align_of_object, offset, size_of_object, elements_size);
			break;
		default:
			assert(false);
			break;
		}
}

void calc_struct_size_align(Ast_Struct_Decl* s) {
	s->align = 1;
	int offset = 0;
	
	for (auto f : s->fields) {
		calc_struct_size_align(s, f->type, offset, 1);
	}
	
	s->size = align_offset(s->align, offset, 0);
	
	// std::cout << s->name << ": s. " << s->size << " a. " << s->align <<"\n";
}

std::tuple<int, int> get_type_size_align(Ast_Type_Decl* type) {
	switch (type->id) {
		case FieldType::Fundamental: {
			int size = get_fundamental_size(cft(type)->token_id);
			return { size, size };
		}
		case FieldType::Struct:
			return { cflat(type)->size, cflat(type)->align };
		case FieldType::Array: {
			auto const [size, align] = get_type_size_align(car(type)->type);
			return { size * car(type)->length, align };
		}
		case FieldType::Vector:
		case FieldType::String:
			return { 8, 4 };
		case FieldType::Enum: {
			const auto size = get_fundamental_size(cenum(type)->token_id);
			return { size, size };
		}
		case FieldType::Alias:
			return get_type_size_align(calias(type)->get_real_type());
		default:
			assert(false);
			break;
		}

	return {};
}

void get_type_id(const Ast_Type_Decl* type, struct_id_t& id, std::string* field_name) {
	switch (type->id) {
	case FieldType::Fundamental:
		id += 'F' + std::to_string(get_fundamental_size(static_cast<const Ast_Fundamental_Type*>(type)->token_id));
		break;
	case FieldType::Struct:
		id += static_cast<const Ast_Struct_Decl*>(type)->name;
		if (field_name) id += *field_name;
		break;
	case FieldType::Array:
		id += 'A';
		get_type_id(static_cast<const Ast_Wrap_Type*>(type)->type, id, nullptr);
		break;
	case FieldType::Vector:
		id += 'V';
		get_type_id(static_cast<const Ast_Wrap_Type*>(type)->type, id, nullptr);
		break;
	case FieldType::String:
		id += 'S';
		break;
	case FieldType::Object:
		id += 'O';
		break;
	case FieldType::Enum:
		id += 'E' + cenum(type)->name;
		break;
	case FieldType::Alias:
		id += 'L' + calias(type)->name;
		break;
	default:
		assert(false);
		break;
	}
}

struct_id_t get_function_struct_id(const Ast_Struct_Decl* s) {
	std::string id;
	int param_n = 0;
	for (auto f : s->fields) {
		id += std::to_string(++param_n);
		get_type_id(f->type, id, &f->name);
	}
	return id;
}

bool is_flat(Ast_Type_Decl* type) {
	switch (type->id) {
	case FieldType::Fundamental:
	case FieldType::Enum:
		return true;
	case FieldType::Struct:
		return cflat(type)->flat;
	case FieldType::Alias:
		return is_flat(calias(type)->type);
	case FieldType::Array:
		return is_flat(car(type)->type);
	case FieldType::Vector:
	case FieldType::String:
	case FieldType::Object:
	case FieldType::Interface:
		return false;
	default:
		assert(false);
		return false;
	}
}