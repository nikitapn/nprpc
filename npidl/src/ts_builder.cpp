// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "ts_builder.hpp"
#include <iostream>
#include <cassert>
#include <string_view>
#include <algorithm>
#include "utils.hpp"
#include <map>

static std::string_view fundamental_to_ts(TokenId id);

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

static const int token_mod_addr = std::ios_base::xalloc();

template<int _Mod> 
struct token_os_mod {
	static constexpr int _mod = _Mod;
};

template<int _Write>
struct read_write_field : token_os_mod<_Write> {
	inline static int offset_addr = std::ios_base::xalloc();
	int offset;
	explicit read_write_field(int o) : offset(o) {}
};

template<int _Mod>
static std::ostream& operator << (std::ostream& os, const token_os_mod<_Mod>& /* field */) {
	os.iword(token_mod_addr) = token_os_mod<_Mod>::_mod;
	return os;
}

template<int _Mod>
static std::ostream& operator << (std::ostream& os, const read_write_field<_Mod>& field) {
	os.iword(token_mod_addr) = read_write_field<_Mod>::_mod;
	os.iword(read_write_field<_Mod>::offset_addr) = field.offset;
	return os;
}

using read_field = read_write_field<0>;
using write_field = read_write_field<1>;
using _token_type = token_os_mod<2>;
constexpr auto toktype = _token_type{};

static std::ostream& operator << (std::ostream& os, const TokenId& token_id) {
	const auto token_mod = os.iword(token_mod_addr);
	if (token_mod == 0) {
		const auto offset = os.iword(read_field::offset_addr);
		switch (token_id) {
		case TokenId::Boolean:
			os << "(this.buffer.dv.getUint8" << "(this.offset+" << offset << ") === 0x01)";
			break;
		case TokenId::Int8:
			os << "this.buffer.dv.getInt8" << "(this.offset+" << offset << ")";
			break;
		case TokenId::UInt8:
			os << "this.buffer.dv.getUint8" << "(this.offset+" << offset << ")";
			break;
		case TokenId::Int16:
			os << "this.buffer.dv.getInt16" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::UInt16:
			os << "this.buffer.dv.getUint16" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::Int32:
			os << "this.buffer.dv.getInt32" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::UInt32:
			os << "this.buffer.dv.getUint32" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::Int64:
			os << "this.buffer.dv.getBigInt64" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::UInt64:
			os << "this.buffer.dv.getBigUint64" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::Float32:
			os << "this.buffer.dv.getFloat32" << "(this.offset+" << offset << ",true)";
			break;
		case TokenId::Float64:
			os << "this.buffer.dv.getFloat64" << "(this.offset+" << offset << ",true)";
			break;
		default:
			assert(false);
		}
	} else if (token_mod == 1) {
		const auto offset = os.iword(read_field::offset_addr);
		switch (token_id) {
		case TokenId::Boolean:
			os << "this.buffer.dv.setUint8" << "(this.offset+" << offset << ", value === true ? 0x01 : 0x00)";
			break;
		case TokenId::Int8:
			os << "this.buffer.dv.setInt8" << "(this.offset+" << offset << ",value)";
			break;
		case TokenId::UInt8:
			os << "this.buffer.dv.setUint8" << "(this.offset+" << offset << ",value)";
			break;
		case TokenId::Int16:
			os << "this.buffer.dv.setInt16" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::UInt16:
			os << "this.buffer.dv.setUint16" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::Int32:
			os << "this.buffer.dv.setInt32" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::UInt32:
			os << "this.buffer.dv.setUint32" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::Int64:
			os << "this.buffer.dv.setBigInt64" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::UInt64:
			os << "this.buffer.dv.setBigUint64" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::Float32:
			os << "this.buffer.dv.setFloat32" << "(this.offset+" << offset << ",value,true)";
			break;
		case TokenId::Float64:
			os << "this.buffer.dv.setFloat64" << "(this.offset+" << offset << ",value,true)";
			break;
		default:
			assert(false);
		}
	} else if (token_mod == 2) {
		//os << fundamental_to_ts(token_id);
		
		switch (token_id) {
		case TokenId::Boolean:	os << "boolean"; break;
		case TokenId::Int8:			os << "i8"; break;
		case TokenId::UInt8:		os << "u8"; break;
		case TokenId::Int16:		os << "i16"; break;
		case TokenId::UInt16:		os << "u16"; break;
		case TokenId::Int32:		os << "i32"; break;
		case TokenId::UInt32:		os << "u32"; break;
		case TokenId::Int64:		os << "i64"; break;
		case TokenId::UInt64:		os << "u64"; break;
		case TokenId::Float32:	os << "f32"; break;
		case TokenId::Float64:	os << "f64"; break;
		default: assert(false);
		}

	} else {
		assert(false);
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const TypescriptBuilder::_ns& ns) {
	if (
		(ns.bulder_.always_full_namespace_ || ns.nm != ns.bulder_.ctx_.nm_cur()) &&
		(ns.nm->parent() != nullptr && !ns.nm->parent()->name().empty())
		) {
		os << ns.nm->to_ts_namespace() << ".";
	}
	return os;
}

static std::string_view fundamental_to_ts(TokenId id) {
	using namespace std::string_view_literals;
	switch (id) {
	case TokenId::Boolean:
		return "boolean"sv;
	case TokenId::Int8:
	case TokenId::UInt8:
	case TokenId::Int16:
	case TokenId::UInt16:
	case TokenId::Int32:
	case TokenId::UInt32:
	case TokenId::Float32:
	case TokenId::Float64: 
		return "number"sv;
	case TokenId::Int64:
	case TokenId::UInt64: 
		return "bigint"sv;
	default: 
		assert(false); 
		return ""sv;
	}
}

static std::string_view get_typed_array_name(TokenId id) {
	switch (id) {
	case TokenId::Boolean:
		// TODO: need implement packing and unpacking bits into bytes
		// in C++ builder and here
		assert(false && "Typed array for boolean is not implemented");
		return "Uint8Array";
	case TokenId::Int8:
		return "Int8Array";
	case TokenId::UInt8:
		return "Uint8Array";
	case TokenId::Int16:
		return "Int16Array";
	case TokenId::UInt16:
		return "Uint16Array";
	case TokenId::Int32:
		return "Int32Array";
	case TokenId::UInt32:
		return "Uint32Array";
	case TokenId::Int64:
		return "BigInt64Array";
	case TokenId::UInt64:
		return "BigUint64Array";
	case TokenId::Float32:
		return "Float32Array";
	case TokenId::Float64:
		return "Float64Array";
	default:
		assert(false);
		return "/*unknown typed array*/";
	}
}

void TypescriptBuilder::emit_type(AstTypeDecl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << toktype << fundamental_to_ts(cft(type)->token_id) << "/*" << cft(type)->token_id << "*/";
		break;
	case FieldType::Struct:
		os << ns(cflat(type)->nm) << cflat(type)->name;
		break;
	case FieldType::Vector:
	case FieldType::Array:
	{
		auto ut = cwt(type)->type;
		if (ut->id == FieldType::Fundamental) {
			// Using typed arrays for direct fundamental types (e.g., vector<u32>)
			os << get_typed_array_name(cft(ut)->token_id);
		} else {
			// For aliases, enums, structs, etc. - preserve the type name
			// (e.g., using DatabaseId = u32; vector<DatabaseId> → Array<DatabaseId>)
			os << "Array<" << emit_type(ut) << ">";
		}
		break;
	}
	case FieldType::String:
		os << "string";
		break;
	case FieldType::Void:
		os << "void";
		break;
	case FieldType::Object:
		os << "NPRPC.ObjectProxy";
		break;
	case FieldType::Alias:
		os << ns(calias(type)->nm) << calias(type)->name;
		break;
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	case FieldType::Optional:
		emit_type(cwt(type)->type, os);
		break;
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_variable(AstTypeDecl* type, std::string name, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << bl() << "let " << name << ": " << toktype << fundamental_to_ts(cft(type)->token_id) << "/*" << cft(type)->token_id << "*/;\n";
		break;
	case FieldType::Struct:
		os << bl() << "let " << name << ": " << cflat(type)->name << " = {} as " << cflat(type)->name << ";\n";
		break;
	case FieldType::Vector:
	case FieldType::Array:
		os << bl() << "let " << name << ": Array<" << emit_type(cwt(type)->type) << "> = [];\n";
		break;
	case FieldType::String:
		os << bl() << "let " << name << ": string = '';\n";
		break;
	case FieldType::Void:
		os << bl() << "let " << name << ": void;\n";
		break;
	case FieldType::Object:
		os << bl() << "let " << name << ": NPRPC.detail.ObjectId = new NPRPC.detail.ObjectId();\n";
		break;
	case FieldType::Alias:
		emit_variable(calias(type)->get_real_type(), name, os);
		break;
	case FieldType::Enum:
		os << bl() << "let " << name << ": " << ns(cenum(type)->nm) << cenum(type)->name << ";\n";
		break;
	case FieldType::Optional:
		emit_variable(cwt(type)->type, name, os);
		break;
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_flat_type(AstTypeDecl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << cft(type)->token_id;
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		os << ns(s->nm) << "Flat_" << ctx_.current_file() << '.' << s->name;
		break;
	}
	case FieldType::Vector: {
		auto wt = cwt(type)->real_type();
		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out << "NPRPC.Flat.Vector_Direct1_" << toktype << cft(wt)->token_id;
		} else if (wt->id == FieldType::Struct) {
			out << "NPRPC.Flat.Vector_Direct2<" << emit_flat_type(wt) << "_Direct>";
		}
		break;
	}
	case FieldType::String:
		assert(false);
		break;
	case FieldType::Array:
		assert(false);
		break;
	case FieldType::Optional:
		assert(false);
		break;
	case FieldType::Object:
		os << "NPRPC.detail.Flat_nprpc_base.ObjectId";
		break;
	case FieldType::Alias: 
		emit_flat_type(calias(type)->get_real_type(), os);
		break;
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_accessors(const std::string& flat_name, AstFieldDecl* f, int& last_field_ended) {
	switch (f->type->id) {
	case FieldType::Fundamental: 
	case FieldType::Enum:
	{
		const int size = get_fundamental_size(cft(f->type)->token_id);
		const int offset = align_offset(size, last_field_ended, size);

		out << bl() << "public get " << f->name <<
			"() { return " << read_field(offset) << cft(f->type)->token_id << "; }\n";

		if (f->type->id == FieldType::Fundamental) {
			out << bl() << "public set " << f->name <<
			"(value: " << fundamental_to_ts(cft(f->type)->token_id) << ") { " << write_field(offset) << cft(f->type)->token_id << "; }\n";
		} else {
			out << bl() << "public set " << f->name <<
			"(value: " <<  ns(cenum(f->type)->nm) << cenum(f->type)->name << ") { " << write_field(offset) << cft(f->type)->token_id << "; }\n";
		}
		
		break;
	}
	case FieldType::Struct: 
		out << bl() << "public get " << f->name << "() { return new " << cflat(f->type)->name
			<< "_Direct(this.buffer, this.offset + " 
			<< align_offset(cflat(f->type)->align, last_field_ended, cflat(f->type)->size) << "); }\n";
		break;

	case FieldType::Vector: {
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		auto const vec_offset = align_offset(v_align, last_field_ended, v_size);

		out <<
			bl() << "public " << f->name << "(elements_size: number): void {\n" << bb(false) <<
				bl() << "NPRPC.Flat._alloc(this.buffer, this.offset + " << vec_offset << ", elements_size, " << ut_size << ", " << ut_align << ");\n" <<
			eb();

		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out <<
				bl () << "public " << f->name << "_d() { return new NPRPC.Flat.Vector_Direct1_" << toktype << cft(wt)->token_id <<
				"(this.buffer, this.offset + " << vec_offset << "); }\n";
		} else if (wt->id == FieldType::Struct) {
			out << bl() << "public " << f->name << "_d() { return new NPRPC.Flat.Vector_Direct2<" << emit_flat_type(wt) << "_Direct>"
				"(this.buffer, this.offset + " << vec_offset << ", " << ut_size << ", " << emit_flat_type(wt) << "_Direct); }\n";
		}

		break;
	}
	case FieldType::Array: {
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		auto const vec_offset = align_offset(v_align, last_field_ended, v_size);
		auto const arr_length = car(f->type)->length;

		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out << bl() << "public " << f->name << "_d() { return new NPRPC.Flat.Array_Direct1_" << toktype << cft(wt)->token_id <<
				"(this.buffer, this.offset + " << vec_offset << ", " << arr_length <<"); }\n";
		} else if (wt->id == FieldType::Struct) {
			out << bl() << "public " << f->name << "_d() { return new NPRPC.Flat.Array_Direct2<"; emit_flat_type(wt, out); out << "_Direct>"
				"(this.buffer, this.offset + " << vec_offset << ", " << ut_size << ", "; emit_flat_type(wt, out); out << "_Direct" << ", " << arr_length << "); }\n";
		} 
		
		break;
	}
	case FieldType::Optional: {
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		auto const optional_offset = align_offset(v_align, last_field_ended, v_size);

		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out <<
				bl() << "public get " << f->name << "() {\n" << bb(false) <<
					bl() << "return new NPRPC.Flat.Optional_Direct1_" << toktype << cft(wt)->token_id <<
				"(this.buffer, this.offset + " << optional_offset << ", " << ut_size << ", " << ut_align <<");\n" <<
				eb();
		} else if (wt->id == FieldType::Struct) {
			out <<
				bl () << "public get " << f->name << "() {\n" << bb(false) <<
					bl() << "return new NPRPC.Flat.Optional_Direct2<"; emit_flat_type(wt, out); out << "_Direct>"
				"(this.buffer, this.offset + " << optional_offset << ", " << ut_size << ", " << ut_align << ", "; emit_flat_type(wt, out); out << "_Direct);\n" <<
				eb();
		} else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
			auto element_type = cwt(wt)->real_type();
			switch (element_type->id) {
				case FieldType::Fundamental:
				{
					std::string opt_container;
					auto token = cft(element_type)->token_id;
					switch (token) {
					case TokenId::Boolean:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_boolean";
						break;
					case TokenId::Int8:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_i8";
						break;
					case TokenId::UInt8:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_u8";
						break;
					case TokenId::Int16:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_i16";
						break;
					case TokenId::UInt16:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_u16";
						break;
					case TokenId::Int32:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_i32";
						break;
					case TokenId::UInt32:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_u32";
						break;
					case TokenId::Int64:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_i64";
						break;
					case TokenId::UInt64:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_u64";
						break;
					case TokenId::Float32:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_f32";
						break;
					case TokenId::Float64:
						opt_container = "NPRPC.Flat.Optional_Direct1_Vector_f64";
						break;
					default:
						assert(false);
					};
					out <<
						bl() << "public get " << f->name << "() {\n" << bb(false) <<
							bl() << "return new " << opt_container << "(this.buffer, this.offset + " << optional_offset << ", " << ut_size << ", " << ut_align << ");\n" <<
						eb();
					break;
				}
				case FieldType::Struct:
				case FieldType::String:
					out <<
						bl() << "public get " << f->name << "() {\n" << bb(false) <<
							bl() << "return new NPRPC.Flat.Optional_Direct2(this.buffer, this.offset + " << optional_offset << ", " << ut_size << ", " << ut_align << ", " << emit_flat_type(wt) << ");\n" <<
						eb();
					break;
				case FieldType::Enum:
					assert(false && "Optional enum arrays are not supported");
					break;
				case FieldType::Alias:
					assert(false && "Optional alias arrays are not supported");
					break;
				default:
					assert(false && "Optional can only be used with fundamental, enum, struct, array, vector or alias types");
			};
		} else {
			assert(false && "Optional can only be used with fundamental, enum, struct, array, vector or alias types");
		}
		
		break;
	}
	case FieldType::String: {
		const int offset_addr = align_offset(4, last_field_ended, 8);
		// const int n_addr = align_offset(4, last_field_ended, 4);

		out <<
		// Getter
			bl() << "public get " << f->name << "() {\n" << bb(false) <<
				bl() << "const offset = this.offset + " << offset_addr << ";\n" << 
				bl() << "const n = this.buffer.dv.getUint32(offset + 4, true);\n" <<
				bl() << "return n > 0 ? u8dec.decode(new DataView(this.buffer.array_buffer, offset + this.buffer.dv.getUint32(offset, true), n)) : \"\"\n" <<
			eb() <<
		// Setter
			bl() << "public set " << f->name << "(str: string) {\n" << bb(false) <<
				bl() << "const bytes = u8enc.encode(str);\n" <<
				bl() << "const offset = NPRPC.Flat._alloc(this.buffer, this.offset + " << offset_addr << ", bytes.length, 1, 1);\n" <<
				bl() << "new Uint8Array(this.buffer.array_buffer, offset).set(bytes);\n" <<
			eb();

		break;
	}
	case FieldType::Object: 
		out <<
			bl() << "public get " << f->name << "() {\n" << bb(false) << 
				bl() << "return new NPRPC.detail.Flat_nprpc_base.ObjectId_Direct(this.buffer, this.offset + " << align_offset(align_of_object, last_field_ended, size_of_object) << ");\n" <<
			eb();
		break;
	case FieldType::Alias: {
		auto temp = std::make_unique<AstFieldDecl>();
		temp->name = f->name;
		temp->type = calias(f->type)->get_real_type();
		emit_accessors(flat_name, temp.get(), last_field_ended);
		break;
	}
	default:
		assert(false);
		break;
	}
}

void TypescriptBuilder::emit_parameter_type_for_servant_callback_r(AstTypeDecl* type, std::ostream& os, const bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_ts(cft(type)->token_id);
		break;
	case FieldType::Struct:
		emit_flat_type(type, os); os << "_Direct";
		break;
	case FieldType::Array: {
		auto wt = cwt(type)->real_type();
		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out << "typeof NPRPC.Flat.Array_Direct1_" << toktype << cft(wt)->token_id;
		} else if (wt->id == FieldType::Struct) {
			out << "NPRPC.Flat.Array_Direct2<"; emit_parameter_type_for_servant_callback_r(wt, out, input); out << ">";
		}
		break;
	}
	case FieldType::Vector: {
		auto wt = cwt(type)->real_type();
		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out << "typeof NPRPC.Flat.Vector_Direct1_" << toktype << cft(wt)->token_id;
		} else if (wt->id == FieldType::Struct) {
			out << "NPRPC.Flat.Vector_Direct2<"; emit_parameter_type_for_servant_callback_r(wt, out, input); out << ">";
		}
		break;
	}
	case FieldType::Optional: {
		auto wt = cwt(type)->real_type();
		if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
			out << "typeof NPRPC.Flat.Optional_Direct1_" << toktype << cft(wt)->token_id;
		} else if (wt->id == FieldType::Struct) {
			out << "NPRPC.Flat.Optional_Direct2<"; emit_parameter_type_for_servant_callback_r(wt, out, input); out << ">";
		}
		break;
	}
	case FieldType::String:
		if (input) {
			os << "string";
		} else {
			os << "NPRPC.String_Direct1";
		}
		break;
	case FieldType::Object:
		if (input) {
			os << "NPRPC.ObjectProxy";
		} else {
			os << "NPRPC.detail.Flat_nprpc_base.ObjectId_Direct";
		}
		break;
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	case FieldType::Alias: {
		if (!input) {
			os << ns(calias(type)->nm) << calias(type)->name;
		} else {
			auto rt = calias(type)->get_real_type();
			if (rt->id == FieldType::Fundamental) {
				os << ns(calias(type)->nm) << calias(type)->name;
			} else {
				emit_parameter_type_for_servant_callback_r(calias(type)->get_real_type(), os, input);
			}
		}
		break;
	}
	case FieldType::Interface:
		assert(false);
		break;
	default:
		assert(false);
		break;
	}
}

void TypescriptBuilder::emit_parameter_type_for_servant_callback(AstFunctionArgument* arg, std::ostream& os) {
	auto const input = (arg->modifier == ArgumentModifier::In);
	emit_parameter_type_for_servant_callback_r(arg->type, os, input);
//	if (!input &&
//		(arg->type->id != FieldType::Vector
//			&& arg->type->id != FieldType::Object
//			&& arg->type->id != FieldType::String)) {
//		os << '&';
//	}
}


void TypescriptBuilder::emit_parameter_type_for_proxy_call_r(AstTypeDecl* type, std::ostream& os, bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_ts(cft(type)->token_id);
		break;
	case FieldType::Struct:
		os << ns(ctx_.nm_cur()) << cflat(type)->name;
		break;
	case FieldType::Array:
	case FieldType::Vector: {
		auto ut = cwt(type)->type;
		if (ut->id == FieldType::Fundamental) {
			os << get_typed_array_name(cft(ut)->token_id);
		} else {
			os << "Array<"; emit_parameter_type_for_proxy_call_r(ut, os, input); os << ">";
		}
		if (type->id == FieldType::Array)
			os << "/*" << car(type)->length << "*/";
		break;
	}
	case FieldType::String:
		os << "string";
		break;
	case FieldType::Optional:
		emit_parameter_type_for_proxy_call_r(copt(type)->type, os, input);
		break;
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	case FieldType::Alias:
		os << ns(calias(type)->nm) << calias(type)->name;
		break;
	case FieldType::Void:
		os << "void";
		break;
	case FieldType::Object:
		// Both input and output use ObjectProxy in the proxy API
		// Input: we extract .data to get ObjectId for marshalling
		// Output: we convert ObjectId to ObjectProxy using create_object_from_oid
		os << "NPRPC.ObjectProxy";
		break;
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_parameter_type_for_proxy_call(AstFunctionArgument* arg, std::ostream& os) {
	const bool input = (arg->modifier == ArgumentModifier::In);
	os << (input ? "/*in*/" : "/*out*/");
	const bool as_reference = arg->modifier == ArgumentModifier::Out;
	if (as_reference)
		os << "NPRPC.ref<";
	if (!input && arg->direct)
		emit_parameter_type_for_servant_callback_r(arg->type, os, input);
	else
		emit_parameter_type_for_proxy_call_r(arg->type, os, input);

	if (as_reference)
		os << '>';
}


void TypescriptBuilder::assign_from_ts_type(AstTypeDecl* type, std::string op1, std::string op2, bool from_iterator) {
	switch (type->id) {
	case FieldType::Fundamental:
	case FieldType::String:
	case FieldType::Enum:
		out << bl() << op1 << " = " << op2 << ";\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		for (auto field : s->fields) {
			assign_from_ts_type(field->type, op1 + (from_iterator ? "." : ".") + field->name, op2 + '.' + field->name);
		}
		break;
	}
	case FieldType::Vector:
		out << bl() << op1 << '(' << op2 << ".length);\n";
		[[fallthrough]];
	case FieldType::Array: {
		auto ut = cwt(type)->type;
		auto real_type = cwt(type)->real_type();
		if (is_fundamental(real_type)) {
			// auto [size, align] = get_type_size_align(wt);
			out << bl() << op1 << "_d()." << (ut->id == FieldType::Fundamental ? "copy_from_typed_array(": "copy_from_ts_array(") 
				<< op2 << "); \n";
		} else {
			out <<
				bb() <<
					bl() << "let vv = " << op1 << "_d(), index = 0;\n" <<
				  bl() << "for (let e of vv)\n" <<
					bb();
						assign_from_ts_type(real_type, "e", op2 + "[index]", true); out <<
						bl() << "++index;\n" <<
					eb() <<
				eb();
		}
		break;
	}
	case FieldType::Optional: {
		auto wt = cwt(type)->real_type();
		if (is_fundamental(wt)) {
			// auto [size, align] = get_type_size_align(wt);
			out << 
				bb() <<
					bl() << "let opt = " << op1 << ";\n" <<
					bl() << "if (" << op2 <<") {\n" << bb(false) <<
						bl() << "opt.alloc();\n" <<
						bl() << "opt.value = " << op2 << "!\n" <<
					eb(false) <<
					bl() << "} else {\n" << bb(false) <<
						bl() << "opt.set_nullopt();\n" <<
					eb() <<
				eb()
				;
		} else {
			out <<
				bb() <<
					bl() << "let opt = " << op1 << ";\n" <<
					bl() << "if (" << op2 << ") {\n" << bb(false) <<
						bl() << "let opt = " << op1 << ";\n" <<
						bl() << "opt.alloc();\n"; // <<
						// bl() << "let value = opt.value;\n";
						// Checked for nullopt above, but TS doesn't know that
						// unless we use "!" here, or create a temporary variable
						assign_from_ts_type(wt, "opt.value", op2 + "!", true); out <<
					eb(false) <<
					bl() << "} else {\n" << bb(false) <<
						bl() << "opt.set_nullopt();\n" <<
					eb() <<
				eb()
				;
		}
		break;
	}
	case FieldType::Alias:
		assign_from_ts_type(calias(type)->type, op1, op2, from_iterator);
		break;
	case FieldType::Object:
		out << bl() << "NPRPC.oid_assign_from_ts(" << op1 << ", " << op2 << ");\n";
		break;
	default:
		assert(false);
		break;
	}
}

void TypescriptBuilder::assign_from_flat_type(
	AstTypeDecl* type,
	std::string op1,
	std::string op2,
	bool from_iterator,
	bool top_object,
	bool direct)
{
	static int _idx = 0;
	switch (type->id) {
	case FieldType::Fundamental:
	case FieldType::String:
	case FieldType::Enum:
		out << bl() << op1 << " = " << op2 << ";\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		out << bl() << op1 << " = {} as " << s->name << ";\n";
		for (auto field : s->fields)
			assign_from_flat_type(field->type, op1 + (from_iterator ? "." : ".") + field->name, op2 + '.' + field->name, false, false);
		break;
	}
	case FieldType::Array:
	case FieldType::Vector: {
		auto ut = cwt(type)->type;
		auto real_type = cwt(type)->real_type();
		if (top_object && direct) {
			out << bl() << op1 << " = " << op2 << "_d();\n";
			break;
		}
		auto idxs = "index_" + std::to_string(_idx++);
		if (is_fundamental(real_type)) {
			// assert(!top_object);
			out <<
				bb() <<
					bl() << op1 << " = " << op2 << "_d()" << (ut->id == FieldType::Fundamental ? ".typed_array\n" : ".array;\n") <<
				eb();
		} else {
			out <<
				bb() <<
					bl() << "let vv = " << op2 << "_d(), " << idxs << " = 0;\n";
			if (top_object)
				out << bl() << op1 << ".length = vv.elements_size;\n";
			else
				out << bl() << "(" << op1 << " as Array<any>) = new Array<any>(vv.elements_size)\n";
			out <<
				bl() << "for (let e of vv) {\n" << bb(false);
						assign_from_flat_type(real_type, op1 + '[' + idxs + ']', "e", true, false);
			out <<
						bl() << "++" << idxs << ";\n" <<
					eb() <<
				eb()
				;
		}
		break;
	}
	case FieldType::Optional: {
		auto wt = cwt(type)->real_type();
		if (is_fundamental(wt)) {
			// auto [size, align] = get_type_size_align(wt);
			out <<
				bb() <<
					bl() << "if (" << op2 << ".has_value) {\n" << bb(false) <<
						bl() << op1 << " = " << op2 << ".value\n" <<
					eb(false) <<
					bl() << "} else {\n" << bb(false) <<
						bl() << op1 << " = undefined\n" <<
					eb() <<
				eb()
				;
		} else {
			out <<
				bb() <<
					bl() << "let opt = " << op2 << ";\n" <<
					bl() << "if (opt.has_value) {\n" << bb(false); // <<
						// bl() << "let value = opt.value;\n";
						assign_from_flat_type(wt, op1 + '!', "opt.value", false, false); out <<
					eb(false) <<
					bl() << "} else {\n" << bb(false) <<
						bl() << op1 << " = undefined\n" <<
					eb() <<
				eb()
				;
		}
		break;
	}
	case FieldType::Alias:
		assign_from_flat_type(calias(type)->get_real_type(), op1, op2, from_iterator, top_object);
		break;
	case FieldType::Object:
		if (true || top_object) {
			// expecting out passed by reference
			out << bl() << op1 << " = NPRPC.create_object_from_flat(" << op2 << ", this.endpoint);\n";
		} else {
			out << bl() << op1 << " = NPRPC.oid_create_from_flat(" << op2 << ");\n";
		}
		break;
	default:
		assert(false);
		break;
	}
}

void TypescriptBuilder::emit_struct2(AstStructDecl* s, bool is_exception) {
	calc_struct_size_align(s);

	// native typescript
	if (!is_exception) {
		out << "export interface " << s->name << " {\n";
		for (auto const f : s->fields) {
			out << "  " << f->name << (f->is_optional() ? "?: " : ": ");
			// For function argument structs with object types, use ObjectId instead of ObjectProxy
			if (f->function_argument && f->type->id == FieldType::Object) {
				out << "NPRPC.detail.ObjectId";
			} else {
				out << emit_type(f->type);
			}
			out << ";\n";
		}
	} else {
		out <<
			bl() << "export class " << s->name << " extends NPRPC.Exception {\n" << bb(false) <<
				bl() << "constructor(";
		for (size_t ix = 1; ix < s->fields.size(); ++ix) {
			auto f = s->fields[ix];
				out <<
				bl() <<"public " << f->name << (f->is_optional() ? "?: " : ": ") << emit_type(f->type);
			if (ix + 1 < s->fields.size()) out << ", ";
		}
		out << ") { super(\""<< s->name << "\"); }\n";
	}

	out << eb() << "\n";

	// Note: _Direct classes are no longer generated - we use marshal/unmarshal functions instead
	// The emit_accessors() function still exists but is unused and can be removed in the future
}

void TypescriptBuilder::emit_constant(const std::string& name, AstNumber* number) {
	out << bl() << "export const " << name << " = ";
	std::visit(overloaded{
	[&](int64_t x) { 
		out << x;
	},
	[&](float x) { out << x; },
	[&](double x) { out << x; },
	[&](bool x) { out << std::ios::boolalpha << x << std::ios::dec; },
	}, number->value);
	out << ";\n";
}

void TypescriptBuilder::emit_struct(AstStructDecl* s) {
	emit_struct2(s, false);
	emit_marshal_function(s);
	out << '\n';
	emit_unmarshal_function(s);
	out << '\n';
}

void TypescriptBuilder::emit_exception(AstStructDecl* s) {
	assert(s->is_exception());
	emit_struct2(s, true);
	// Only generate unmarshal for exceptions - they're never marshalled, only thrown and unmarshalled
	emit_unmarshal_function(s);
	out << '\n';
}

void TypescriptBuilder::emit_file_footer() {
	// throw_exception function body
	auto& exs = ctx_.exceptions;
	if (!exs.empty()) {

		out << '\n' <<
			bl() << "function " << ctx_.current_file() << "_throw_exception(buf: NPRPC.FlatBuffer): void { \n" << bb(false) <<
				bl() << "switch( buf.read_exception_number() ) {\n" << bb(false)
			;

		always_full_namespace(true);
		for (auto ex : exs) {
			out <<
				bl() << "case " << ex->exception_id << ":\n" <<
				bb()
				;
			// Use unmarshal function instead of _Direct class
			if (ex->fields.size() > 1) {
				out << bl() << "let ex_obj = unmarshal_" << ex->name << "(buf, " << size_of_header << ");\n";
				out << bl() << "throw new " << ns(ex->nm) << ex->name << "(";
				for (size_t i = 1; i < ex->fields.size(); ++i) {
					out << "ex_obj." << ex->fields[i]->name;
					if (i + 1 < ex->fields.size())
						out << ", ";
				}
				out << ");\n";
			} else {
				// Exception has no fields beyond id
				out << bl() << "throw new " << ns(ex->nm) << ex->name << "();\n";
			}
			out << eb(); // case
		}
		always_full_namespace(false);
		out <<
					bl () << "default:\n" << bb(false) <<
						bl() << "throw \"unknown rpc exception\";\n" <<
					eb(false) << // default
				eb() << // switch
			eb() // function
			;
	}

	// other
	emit_arguments_structs([this](AstStructDecl* s) {
		emit_struct2(s, false);
		emit_marshal_function(s);
		out << '\n';
		emit_unmarshal_function(s);
		out << '\n';
	});
	emit_struct_helpers();
}

void TypescriptBuilder::emit_using(AstAliasDecl* u) {
	out << bl() << "export type " << u->name << " = " << emit_type(u->type) << ";\n";
}

void TypescriptBuilder::emit_enum(AstEnumDecl* e) {
	out << bl() << "export enum " << e->name << " { //" << toktype << e->token_id << '\n' << bb(false);
	std::int64_t ix = 0;
	for (size_t i = 0; i < e->items.size(); ++i) {
		out << bl() << e->items[i].first;
		auto const n = e->items[i].second;
		if (n.second || ix != n.first) { // explicit
			out << " = " << n.first;
			ix = n.first.decimal() + 1;
		} else {
			++ix;
		}
		if (i != e->items.size() - 1)
			out << ",\n";
	}
	out << '\n' << eb() << '\n';
}

void TypescriptBuilder::emit_namespace_begin() {
	if (ctx_.nm_cur()->parent() && ctx_.nm_cur()->parent()->name().empty()) return;
	out << bl() << "export namespace " << ctx_.nm_cur()->name() << " { \n" << bb(false);
}

void TypescriptBuilder::emit_namespace_end() {
	if (ctx_.nm_cur()->parent() && ctx_.nm_cur()->parent()->name().empty())
		return;
	out << bl() << "} // namespace " << ctx_.nm_cur()->name() << "\n\n" << eb(false);
}

void TypescriptBuilder::emit_interface(AstInterfaceDecl* ifs) {
	auto const flat_nm = "Flat_" + ctx_.current_file();
	const auto servant_iname = 'I' + ifs->name + "_Servant";

	auto emit_function_arguments = [](bool ts, AstFunctionDecl* fn, std::ostream& os,
		std::function<void(AstFunctionArgument*, std::ostream& os)> emitter) {
			os << '(';
			size_t ix = 0;
			for (auto arg : fn->args) {
				os << arg->name;
				if (!ts)
					os << ": ";
				else
					os << (arg->is_optional() && arg->modifier != ArgumentModifier::Out ? "?: " : ": ");
				emitter(arg, os);
				if (++ix != fn->args.size())
					os << ", ";
			}
			os << ')';
	};


	// Proxy definition =======================================================
	out <<
		bl() << "export class " << ifs->name << ' ';

	//if (ifs->plist.size()) {
		//out << " extends " << ifs->plist[0]->name << "\n";
		//for (size_t i = 1; i < ifs->plist.size(); ++i) {
		//	out << " extends " << ifs->plist[i]->name << "\n";
		//}
		//out << "{\n";
	//} else {
	//}

	out << 
		"extends NPRPC.ObjectProxy {\n" << bb(false) <<
			bl() <<"public static get servant_t(): new() => _"<< servant_iname <<" {\n" << bb(false) <<
				bl() << "return _" << servant_iname << ";\n" <<
			eb() << '\n';
		;

	// parent's functions
	std::map<AstInterfaceDecl*, int> ifs_idxs;
	auto count_all = [&ifs_idxs](AstInterfaceDecl* ifs_inherited, int& n) { 
		ifs_idxs.emplace(ifs_inherited, n);
	};

	int n = 1;
	for (auto parent : ifs->plist) {
		dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
	}
		
	for (auto& inherited_ifs : ifs_idxs) {
		if (inherited_ifs.first->fns.size()) {
			out << bl() << "// " << inherited_ifs.first->name << '\n';
		}
		for (auto& fn : inherited_ifs.first->fns) {
			out << bl() << "public async " << fn->name;
			emit_function_arguments(false, fn, out,
				std::bind(&TypescriptBuilder::emit_parameter_type_for_proxy_call, this, _1, _2)
			);
			out << ": Promise<" << emit_type(fn->ret_value) << "> {\n" << bb(false) <<
				bl() << (!fn->is_void() ? "return " : "") << inherited_ifs.first->name << ".prototype." << fn->name << ".bind(this,";
			for (auto arg : fn->args) {
				out << arg->name << ',';
			}
			out << inherited_ifs.second << ")();\n" << eb();
		}
	}
	// proxy object functions definitions
	out << '\n';
	for (auto& fn : ifs->fns) {
		make_arguments_structs(fn);
		out << bl() << "public async " << fn->name;
		emit_function_arguments(true, fn, out,
			std::bind(&TypescriptBuilder::emit_parameter_type_for_proxy_call, this, _1, _2)
		);
		out << ": Promise<" << emit_type(fn->ret_value) << "> {\n" << bb(false) <<
			bl() << "let interface_idx = (arguments.length == " << fn->args.size() << " ? 0 : arguments[arguments.length - 1]);\n"
			;

		const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
		const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);
		out << 
			bl() << "const buf = NPRPC.FlatBuffer.create();\n" <<
			bl() << "buf.prepare(" << capacity << ");\n" <<
			bl() << "buf.commit(" << fixed_size << ");\n" <<
			bl() << "buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);\n" <<
			bl() << "buf.write_msg_type(NPRPC.impl.MessageType.Request);\n" <<
			bl() << "// Write CallHeader directly\n" <<
			bl() << "buf.dv.setUint16(" << size_of_header << " + 0, this.data.poa_idx, true);\n" <<
			bl() << "buf.dv.setUint8(" << size_of_header << " + 2, interface_idx);\n" <<
			bl() << "buf.dv.setUint8(" << size_of_header << " + 3, " << fn->idx << ");\n" <<
			bl() << "buf.dv.setBigUint64(" << size_of_header << " + 8, this.data.object_id, true);\n"
			;

		if (fn->in_s) {
			// Use new marshal function instead of _Direct wrapper
			out << bl() << "marshal_" << fn->in_s->name << "(buf, " << get_arguments_offset() << ", {";
			
			int ix = 0;
			for (auto in : fn->args) {
				if (in->modifier == ArgumentModifier::Out)
					continue;
				if (ix > 0) out << ", ";
				out << "_" << (ix + 1) << ": ";
				// For Object types, extract the ObjectId from ObjectProxy
				if (in->type->id == FieldType::Object) {
					out << in->name << ".data";
				} else {
					out << in->name;
				}
				++ix;
			}
			out << "});\n";
		}

		out << bl() << "buf.write_len(buf.size - 4);\n";

		out <<
			bl() << "await NPRPC.rpc.call(this.endpoint, buf, this.timeout);\n" <<
			bl() << "let std_reply = NPRPC.handle_standart_reply(buf);\n"
			;

		if (fn->ex) {
			out <<
				bl() << "if (std_reply == 1)" << bb() 
					<< bl() << ctx_.current_file() << "_throw_exception(buf);\n" <<
				eb();
				;
		}

		if (!fn->out_s) {
			out <<
				bl() << "if (std_reply != 0) {\n" << bb(false) <<
					bl() << "console.log(\"received an unusual reply for function with no output arguments\");\n" <<
				eb()
				;
		} else {
			out <<
				bl() << "if (std_reply != -1) {\n" << bb(false) <<
					bl() << "console.log(\"received an unusual reply for function with output arguments\");\n" <<
					bl() << "throw new NPRPC.Exception(\"Unknown Error\");\n" <<
				eb()
				;

			// Use new unmarshal function instead of _Direct wrapper
			out << bl() << "const out = unmarshal_" << fn->out_s->name << "(buf, " << size_of_header << ");\n";

			int ix = fn->is_void() ? 0 : 1;

			for (auto out_arg : fn->args) {
				if (out_arg->modifier == ArgumentModifier::In)
					continue;
				++ix;
				// For Object types, convert ObjectId to ObjectProxy
				if (out_arg->type->id == FieldType::Object) {
					out << bl() << out_arg->name << ".value = NPRPC.create_object_from_oid(out._" << ix << ", this.endpoint);\n";
				} else {
					out << bl() << out_arg->name << ".value = out._" << ix << ";\n";
				}
			}

			if (!fn->is_void()) {
				out << bl() << "return out._1;\n";
			}
		}

		out << eb(); // function
	}

	out << eb(); // Proxy class ends
	
	// Servant definition
	out << bl() << "export interface " << servant_iname << '\n';
	if (ifs->plist.size()) {
		out << " extends I" << ifs->plist[0]->name << "_Servant\n";
		for (size_t i = 1; i < ifs->plist.size(); ++i) {
			out << ", I" << ifs->plist[i]->name << "_Servant\n";
		}
	}
	out << bb();
	for (auto fn : ifs->fns) {
		out << bl() << fn->name;
		emit_function_arguments(false, fn, out,
			std::bind(&TypescriptBuilder::emit_parameter_type_for_proxy_call, this, _1, _2)
		);
		out << ": " << emit_type(fn->ret_value) << ";\n";
	}

	out << eb(); // interface ends

	out <<
		bl() << "export class _" << servant_iname << " extends NPRPC.ObjectServant {\n" << bb(false) <<
			bl() << "public static _get_class(): string { return \"" << ctx_.current_file() << '/' << ctx_.nm_cur()->to_ts_namespace(false) << '.' /*ns(ctx_.nm_cur()) */ << ifs->name << "\"; }\n" <<
			bl() << "public readonly get_class = () => { return _"<< servant_iname << "._get_class(); }\n" <<
			bl() << "public readonly dispatch = (buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean) => {\n" << bb(false) <<
				bl() << "_" << servant_iname << "._dispatch(this, buf, remote_endpoint, from_parent);\n" <<
			eb() <<
		bl() << "static _dispatch(obj: _" << servant_iname << ", buf: NPRPC.FlatBuffer, remote_endpoint: NPRPC.EndPoint, from_parent: boolean): void {\n" << bb(false)
		;

	// Servant dispatch ====================================================
	out << 
		bl() << "// Read CallHeader directly\n" <<
		bl() << "const function_idx = buf.dv.getUint8(" << size_of_header << " + 3);\n"
		;
		
	if (ifs->plist.empty()) {
		// ok
	} else {
		out <<
			bl() << "if (from_parent == false) {\n" << bb(false) <<
				bl() << "const interface_idx = buf.dv.getUint8(" << size_of_header << " + 2);\n" <<
				bl() <<"switch(interface_idx) {\n" << bb(false) <<
					bl() << "case 0:\n" << bb(false) <<
						bl() << "break;\n" <<
					eb(false)
			;
		int ix = 1;
		auto select_interface = [&ix, this, ifs](AstInterfaceDecl* i) {
			if (i == ifs)
				return;
			out <<
				bl() << "case "<< ix << ":\n" << bb(false) <<
					bl() << "_I" << i->name << "_Servant._dispatch(obj, buf, remote_endpoint, true);\n" <<
					bl() << "return;\n" <<
				eb(false)
			;
			++ix;
		};
		
		dfs_interface(select_interface, ifs);
		
		out <<
			bl() << "default:\n" << bb(false) <<
				bl() << "throw \"unknown interface\";\n" <<
				eb(false) <<
			eb() << // switch
			eb() // if from_parent == false
			;
	}

	out <<
			bl() << "switch(function_idx) {\n" << bb(false)
			;

	for (auto fn : ifs->fns) {
		out << bl() << "case " << fn->idx << ": {\n" << bb(false);
		int out_ix = fn->is_void() ? 0 : 1;
		if (fn->out_s) {
			for (auto arg : fn->args) {
				if (arg->modifier == ArgumentModifier::Out) {
					out << bl() << "let _out_" << ++out_ix << ": " << emit_type(arg->type) << ";\n";
				}
			}
		}

		if (fn->in_s) {
			out <<
				bl() << "const ia = unmarshal_" << fn->in_s->name << "(buf, " << get_arguments_offset() << ");\n"
				;
		}

		if (fn->out_s && !fn->out_s->flat) {
			const auto offset = size_of_header;
			const auto initial_size = offset + fn->out_s->size;
			out <<
				bl() << "const obuf = buf;\n" <<
				bl() << "obuf.consume(obuf.size);\n" <<
				bl() << "obuf.prepare(" << initial_size + 128 << ");\n" <<
				bl() << "obuf.commit(" << initial_size << ");\n"
				;
		}

		if (!fn->is_void())
			emit_variable(fn->ret_value, "__ret_val", out);

		if (fn->ex)
			out << bl() << "try {\n" << bb(false);

		out << 
			bl() <<(fn->is_void() ? "" : "__ret_val = ") << "(obj as any)." << fn->name << "("
			;

		size_t in_ix = 0, idx = 0; out_ix = fn->is_void() ? 0 : 1;
		for (auto arg : fn->args) {
			if (arg->modifier == ArgumentModifier::Out) {
				assert(fn->out_s);
				// For output arguments, we'll create refs to pass
				out << bl() << "_out_" << ++out_ix;
			} else {
				// For input arguments, just pass from the unmarshalled object
				out << "ia._" << ++in_ix;
			}
			if (++idx != fn->args.size())
				out << ", ";
		}
		out << ");\n";

		/*
		out_ix = fn->is_void() ? 0 : 1;
		for (auto arg : fn->args) {
			if (arg->modifier == ArgumentModifier::Out) {
				++out_ix;
				if (arg->type->id == FieldType::Object) {
					oc <<
						"{\n"
						"  auto obj = impl::g_orb->get_object(" << "oa._" << out_ix << "().poa_idx(), " << "oa._" << out_ix << "().object_id());\n"
						"  if (obj) if (auto real_obj = (*obj).get(); real_obj) ref_list.add_ref(real_obj);\n"
						"}\n"
						;
				}
			}
		}
		*/

		if (fn->ex) {
			const auto offset = size_of_header;
			const auto initial_size = offset + fn->ex->size;
			
			always_full_namespace(true);
			out <<
				eb() << // try
				bl() << "catch(e) {\n" << bb(false) <<
				bl() << "if (!(e instanceof " << emit_type(fn->ex) << ")) throw e;\n" <<
				bl() << "const obuf = buf;\n" <<
				bl() << "obuf.consume(obuf.size);\n" <<
				bl() << "obuf.prepare(" << initial_size << ");\n" <<
				bl() << "obuf.commit(" << initial_size << ");\n" <<
				bl() << "const ex_data = {__ex_id: " << fn->ex->exception_id;
			always_full_namespace(false);
			for (size_t i = 1; i < fn->ex->fields.size(); ++i) {
				auto mb = fn->ex->fields[i];
				out << ", " << mb->name << ": e." << mb->name;
			}
			out << "};\n";
			always_full_namespace(true);
			out <<
				bl() << ns(fn->ex->nm) << "marshal_" << fn->ex->name << "(obuf, " << offset << ", ex_data);\n";
			always_full_namespace(false);
			out << 
					bl() << "obuf.write_len(obuf.size - 4);\n" <<
					bl() << "obuf.write_msg_id(NPRPC.impl.MessageId.Exception);\n" <<
					bl() << "obuf.write_msg_type(NPRPC.impl.MessageType.Answer);\n" <<
					bl() << "return;\n" <<
				eb() // catch
				;
		}
		if (!fn->out_s) {
			out << bl() << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Success);\n";
		} else {
			if (fn->out_s->flat) { // it means that we are writing output data in the input buffer
				const auto offset = size_of_header;
				const auto initial_size = offset + fn->out_s->size;
				out <<
					bl() << "const obuf = buf;\n" <<
					bl() << "obuf.consume(obuf.size);\n" <<
					bl() << "obuf.prepare(" << initial_size << ");\n" <<
					bl() << "obuf.commit(" << initial_size << ");\n"
					;
			}
			
			// Build output object
			out << bl() << "const out_data = {";
			int ix = 0;
			if (!fn->is_void()) {
				out << "_1: __ret_val";
				ix = 1;
			}
			for (auto out_arg : fn->args) {
				if (out_arg->modifier == ArgumentModifier::In)
					continue;
				if (ix > 0) out << ", ";
				++ix;
				out << "_" << ix << ": ";
				// For Object types, extract the ObjectId from the ObjectProxy
				if (out_arg->type->id == FieldType::Object) {
					out << "_out_" << ix << ".data";
				} else {
					out << "_out_" << ix;
				}
			}
			out << "};\n";
			
			out <<
				bl() << "marshal_" << fn->out_s->name << "(obuf, " << size_of_header << ", out_data);\n" <<
				bl() << "obuf.write_len(obuf.size - 4);\n" <<
				bl() << "obuf.write_msg_id(NPRPC.impl.MessageId.BlockResponse);\n" <<
				bl() << "obuf.write_msg_type(NPRPC.impl.MessageType.Answer);\n"
				;
		}

		out <<
				bl() << "break;\n" <<
			eb(); // case ends
			;
	}

	out <<
						bl() << "default:\n" << bb(false) <<
							bl() << "NPRPC.make_simple_answer(buf, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);\n" <<
						eb(false) << // default ends
					eb() << // switch block ends
				eb() << // dispatch ends
			eb() << '\n' // class ends
			;
}

void TypescriptBuilder::emit_struct_helpers() {
	// Helper functions removed - marshal/unmarshal functions are used instead
	// Old code generated assign_from_flat_* and assign_from_ts_* helpers that used _Direct classes
}

TypescriptBuilder::_ns TypescriptBuilder::ns(Namespace* nm) const {
	return { *this, nm };
}

void TypescriptBuilder::emit_heap_views() {
	// HeapViews is now part of FlatBuffer, no need to generate it here
}

void TypescriptBuilder::emit_field_marshal(AstFieldDecl* f, int& offset, const std::string& data_name) {
	const std::string field_access = data_name + "." + f->name;
	
	switch (f->type->id) {
	case FieldType::Fundamental: {
		const auto token = cft(f->type)->token_id;
		const int size = get_fundamental_size(token);
		const int field_offset = align_offset(size, offset, size);
		
		switch (token) {
		case TokenId::Boolean:
			out << bl() << "buf.dv.setUint8(offset + " << field_offset << ", " << field_access << " ? 1 : 0);\n";
			break;
		case TokenId::Int8:
			out << bl() << "buf.dv.setInt8(offset + " << field_offset << ", " << field_access << ");\n";
			break;
		case TokenId::UInt8:
			out << bl() << "buf.dv.setUint8(offset + " << field_offset << ", " << field_access << ");\n";
			break;
		case TokenId::Int16:
			out << bl() << "buf.dv.setInt16(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::UInt16:
			out << bl() << "buf.dv.setUint16(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::Int32:
			out << bl() << "buf.dv.setInt32(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::UInt32:
			out << bl() << "buf.dv.setUint32(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::Int64:
			out << bl() << "buf.dv.setBigInt64(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::UInt64:
			out << bl() << "buf.dv.setBigUint64(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::Float32:
			out << bl() << "buf.dv.setFloat32(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		case TokenId::Float64:
			out << bl() << "buf.dv.setFloat64(offset + " << field_offset << ", " << field_access << ", true);\n";
			break;
		default:
			assert(false);
		}
		break;
	}
	case FieldType::Enum: {
		const int size = get_fundamental_size(cft(f->type)->token_id);
		const int field_offset = align_offset(size, offset, size);
		out << bl() << "buf.dv.setInt32(offset + " << field_offset << ", " << field_access << ", true);\n";
		break;
	}
	case FieldType::String: {
		const int field_offset = align_offset(4, offset, 8);
		out << bl() << "NPRPC.marshal_string(buf, offset + " << field_offset << ", " << field_access << ");\n";
		break;
	}
	case FieldType::Struct: {
		auto s = cflat(f->type);
		const int field_offset = align_offset(s->align, offset, s->size);
		out << bl() << "marshal_" << s->name << "(buf, offset + " << field_offset << ", " << field_access << ");\n";
		break;
	}
	case FieldType::Object: {
		const int field_offset = align_offset(align_of_object, offset, size_of_object);
		out << bl() << "NPRPC.detail.marshal_ObjectId(buf, offset + " << field_offset << ", " << field_access << ");\n";
		break;
	}
	case FieldType::Array: {
		// Fixed-size array - data is inline at the offset
		auto wt = cwt(f->type)->real_type();
		auto arr = static_cast<AstArrayDecl*>(f->type);
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		if (is_fundamental(wt)) {
			// For fixed-size arrays of fundamentals, copy data directly to buffer
			auto typed_array_name = get_typed_array_name(cft(wt)->token_id);
			out << bl() << "const __arr = new " << typed_array_name << "(buf.array_buffer, offset + " 
			    << field_offset << ", " << arr->length << ");\n";
			out << bl() << "__arr.set(" << field_access << ");\n";
		} else if (wt->id == FieldType::Struct) {
			// For fixed-size arrays of structs, marshal each element in place
			out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n" << bb(false);
			out << bl() << "marshal_" << cflat(wt)->name << "(buf, offset + " << field_offset 
			    << " + i * " << ut_size << ", " << field_access << "[i]);\n";
			out << eb();
		} else {
			assert(false && "Unsupported array element type for marshalling");
		}
		break;
	}
	case FieldType::Vector: {
		// Dynamic vector - data is indirect (ptr + length)
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		if (is_fundamental(wt)) {
			out << bl() << "NPRPC.marshal_typed_array(buf, offset + " << field_offset << ", " 
			    << field_access << ", " << ut_size << ", " << ut_align << ");\n";
		} else if (wt->id == FieldType::Struct) {
			out << bl() << "NPRPC.marshal_struct_array(buf, offset + " << field_offset << ", " 
			    << field_access << ", marshal_" << cflat(wt)->name << ", " << ut_size << ", " << ut_align << ");\n";
		} else {
			assert(false && "Unsupported vector element type for marshalling");
		}
		break;
	}
	case FieldType::Optional: {
		// All optionals have the same layout: 4-byte relative offset (0 = no value)
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		out << 
			bl() << "if (" << field_access << " !== undefined) {\n" << bb(false);
		
		if (is_fundamental(wt)) {
			out << bl() << "NPRPC.marshal_optional_fundamental(buf, offset + " << field_offset 
			    << ", " << field_access << ", " << get_fundamental_size(cft(wt)->token_id) << ");\n";
		} else if (wt->id == FieldType::Struct) {
			auto [wt_size, wt_align] = get_type_size_align(wt);
			out << bl() << "NPRPC.marshal_optional_struct(buf, offset + " << field_offset 
			    << ", " << field_access << ", marshal_" << cflat(wt)->name << ", " 
			    << wt_size << ", " << wt_align << ");\n";
		} else if (wt->id == FieldType::String) {
			// Optional string uses marshal_optional_struct with marshal_string
			out << bl() << "NPRPC.marshal_optional_struct(buf, offset + " << field_offset 
			    << ", " << field_access << ", NPRPC.marshal_string, 8, 4);\n";
		} else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
			// Optional vector/array also uses the optional_struct pattern
			auto real_elem_type = cwt(wt)->real_type();
			auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
			if (is_fundamental(real_elem_type)) {
				// Wrap marshal_typed_array in a lambda
				out << bl() << "NPRPC.marshal_optional_struct(buf, offset + " << field_offset 
				    << ", " << field_access << ", (b, o, v) => NPRPC.marshal_typed_array(b, o, v, " 
				    << ut_size << ", " << ut_align << "), 8, 4);\n";
			} else if (real_elem_type->id == FieldType::Struct) {
				out << bl() << "NPRPC.marshal_optional_struct(buf, offset + " << field_offset 
				    << ", " << field_access << ", (b, o, v) => NPRPC.marshal_struct_array(b, o, v, marshal_" 
				    << cflat(real_elem_type)->name << ", " << ut_size << ", " << ut_align << "), 8, 4);\n";
			} else {
				assert(false && "Unsupported vector element type in optional for marshalling");
			}
		} else {
			assert(false && "Unsupported optional element type for marshalling");
		}
		
		out << 
			eb(false) <<
			bl() << "} else {\n" << bb(false) <<
				bl() << "buf.dv.setUint32(offset + " << field_offset << ", 0, true); // nullopt\n" <<
			eb();
		break;
	}
	case FieldType::Alias: {
		auto real_type = calias(f->type)->get_real_type();
		// Check if this is an alias to a VECTOR (not array) of fundamentals
		// Arrays stay as TypedArrays, but vector aliases use JavaScript arrays for semantic meaning
		if (real_type->id == FieldType::Vector) {
			auto elem_type = cwt(real_type)->real_type();
			if (is_fundamental(elem_type)) {
				// Convert Array<T> to TypedArray for marshalling
				auto [v_size, v_align] = get_type_size_align(real_type);
				auto [ut_size, ut_align] = get_type_size_align(elem_type);
				const int field_offset = align_offset(v_align, offset, v_size);
				auto typed_array_name = get_typed_array_name(cft(elem_type)->token_id);
				auto temp_var = "temp_" + f->name;
				out << bl() << "const " << temp_var << " = new " << typed_array_name << "(" << field_access << ");\n";
				// Marshal the typed array directly (don't recurse, to avoid appending field name again)
				out << bl() << "NPRPC.marshal_typed_array(buf, offset + " << field_offset << ", " 
				    << temp_var << ", " << ut_size << ", " << ut_align << ");\n";
				return;
			}
		}
		// For other aliases (including array aliases), just resolve and marshal
		auto temp_field = *f;
		temp_field.type = real_type;
		emit_field_marshal(&temp_field, offset, data_name);
		return;
	}
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_field_unmarshal(AstFieldDecl* f, int& offset, const std::string& result_name) {
	const std::string field_name = result_name + "." + f->name;
	
	switch (f->type->id) {
	case FieldType::Fundamental: {
		const auto token = cft(f->type)->token_id;
		const int size = get_fundamental_size(token);
		const int field_offset = align_offset(size, offset, size);
		
		switch (token) {
		case TokenId::Boolean:
			out << bl() << field_name << " = buf.dv.getUint8(offset + " << field_offset << ") !== 0;\n";
			break;
		case TokenId::Int8:
			out << bl() << field_name << " = buf.dv.getInt8(offset + " << field_offset << ");\n";
			break;
		case TokenId::UInt8:
			out << bl() << field_name << " = buf.dv.getUint8(offset + " << field_offset << ");\n";
			break;
		case TokenId::Int16:
			out << bl() << field_name << " = buf.dv.getInt16(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::UInt16:
			out << bl() << field_name << " = buf.dv.getUint16(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::Int32:
			out << bl() << field_name << " = buf.dv.getInt32(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::UInt32:
			out << bl() << field_name << " = buf.dv.getUint32(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::Int64:
			out << bl() << field_name << " = buf.dv.getBigInt64(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::UInt64:
			out << bl() << field_name << " = buf.dv.getBigUint64(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::Float32:
			out << bl() << field_name << " = buf.dv.getFloat32(offset + " << field_offset << ", true);\n";
			break;
		case TokenId::Float64:
			out << bl() << field_name << " = buf.dv.getFloat64(offset + " << field_offset << ", true);\n";
			break;
		default:
			assert(false);
		}
		break;
	}
	case FieldType::Enum: {
		const int size = get_fundamental_size(cft(f->type)->token_id);
		const int field_offset = align_offset(size, offset, size);
		out << bl() << field_name << " = buf.dv.getInt32(offset + " << field_offset << ", true);\n";
		break;
	}
	case FieldType::String: {
		const int field_offset = align_offset(4, offset, 8);
		out << bl() << field_name << " = NPRPC.unmarshal_string(buf, offset + " << field_offset << ");\n";
		break;
	}
	case FieldType::Struct: {
		auto s = cflat(f->type);
		const int field_offset = align_offset(s->align, offset, s->size);
		out << bl() << field_name << " = unmarshal_" << s->name << "(buf, offset + " << field_offset << ");\n";
		break;
	}
	case FieldType::Object: {
		const int field_offset = align_offset(align_of_object, offset, size_of_object);
		out << bl() << field_name << " = NPRPC.detail.unmarshal_ObjectId(buf, offset + " << field_offset << ");\n";
		break;
	}
	case FieldType::Array: {
		// Fixed-size array - data is inline at the offset
		auto wt = cwt(f->type)->real_type();
		auto arr = static_cast<AstArrayDecl*>(f->type);
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		if (is_fundamental(wt)) {
			// For fixed-size arrays of fundamentals, create view directly on buffer
			auto typed_array_name = get_typed_array_name(cft(wt)->token_id);
			out << bl() << field_name << " = new " << typed_array_name 
			    << "(buf.array_buffer, offset + " << field_offset 
			    << ", " << arr->length << ");\n";
		} else if (wt->id == FieldType::Struct) {
			// For fixed-size arrays of structs, unmarshal each element
			out << bl() << field_name << " = new Array(" << arr->length << ");\n";
			out << bl() << "for (let i = 0; i < " << arr->length << "; ++i) {\n" << bb(false);
			out << bl() << field_name << "[i] = unmarshal_" << cflat(wt)->name 
			    << "(buf, offset + " << field_offset << " + i * " << ut_size << ");\n";
			out << eb();
		} else {
			assert(false && "Unsupported array element type for unmarshalling");
		}
		break;
	}
	case FieldType::Vector: {
		// Dynamic vector - data is indirect (ptr + length)
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(wt);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		if (is_fundamental(wt)) {
			// Cast to specific typed array type (e.g., Uint8Array for uuid_t)
			out << bl() << field_name << " = NPRPC.unmarshal_typed_array(buf, offset + " << field_offset 
			    << ", " << ut_size << ") as " << get_typed_array_name(cft(wt)->token_id) << ";\n";
		} else if (wt->id == FieldType::Struct) {
			out << bl() << field_name << " = NPRPC.unmarshal_struct_array(buf, offset + " << field_offset 
			    << ", unmarshal_" << cflat(wt)->name << ", " << ut_size << ");\n";
		} else {
			assert(false && "Unsupported vector element type for unmarshalling");
		}
		break;
	}
	case FieldType::Optional: {
		// All optionals have the same layout: 4-byte relative offset (0 = no value)
		auto wt = cwt(f->type)->real_type();
		auto [v_size, v_align] = get_type_size_align(f->type);
		const int field_offset = align_offset(v_align, offset, v_size);
		
		// Check if the relative offset is non-zero
		out << 
			bl() << "if (buf.dv.getUint32(offset + " << field_offset << ", true) !== 0) {\n" << bb(false);
		
		if (is_fundamental(wt)) {
			out << bl() << field_name << " = NPRPC.unmarshal_optional_fundamental(buf, offset + " << field_offset 
			    << ", " << get_fundamental_size(cft(wt)->token_id) << ");\n";
		} else if (wt->id == FieldType::Struct) {
			auto [wt_size, wt_align] = get_type_size_align(wt);
			out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset 
			    << ", unmarshal_" << cflat(wt)->name << ", " << wt_align << ");\n";
		} else if (wt->id == FieldType::String) {
			// Optional string uses unmarshal_optional_struct with unmarshal_string
			out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset 
			    << ", NPRPC.unmarshal_string, 4);\n";
		} else if (wt->id == FieldType::Vector || wt->id == FieldType::Array) {
			// Optional vector/array
			auto real_elem_type = cwt(wt)->real_type();
			auto [ut_size, ut_align] = get_type_size_align(real_elem_type);
			if (is_fundamental(real_elem_type)) {
				auto typed_array_name = get_typed_array_name(cft(real_elem_type)->token_id);
				out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset 
				    << ", (b, o) => NPRPC.unmarshal_typed_array(b, o, " << ut_size << ") as " 
				    << typed_array_name << ", 4) as " << typed_array_name << ";\n";
			} else if (real_elem_type->id == FieldType::Struct) {
				out << bl() << field_name << " = NPRPC.unmarshal_optional_struct(buf, offset + " << field_offset 
				    << ", (b, o) => NPRPC.unmarshal_struct_array(b, o, unmarshal_" 
				    << cflat(real_elem_type)->name << ", " << ut_size << "), 4);\n";
			} else {
				assert(false && "Unsupported vector element type in optional for unmarshalling");
			}
		} else {
			assert(false && "Unsupported optional element type for unmarshalling");
		}
		
		out << 
			eb(false) <<
			bl() << "} else {\n" << bb(false) <<
				bl() << field_name << " = undefined;\n" <<
			eb();
		break;
	}
	case FieldType::Alias: {
		auto real_type = calias(f->type)->get_real_type();
		// Check if this is an alias to a VECTOR (not array) of fundamentals
		// Arrays stay as TypedArrays, but vector aliases use JavaScript arrays for semantic meaning
		if (real_type->id == FieldType::Vector) {
			auto elem_type = cwt(real_type)->real_type();
			if (is_fundamental(elem_type)) {
				// Unmarshal as TypedArray then convert to Array<T>
				auto temp_var = "temp_" + f->name;
				out << bl() << "const " << temp_var << " = ";
				auto temp_field = *f;
				temp_field.type = real_type;
				temp_field.name = temp_var;
				// Generate the unmarshal call inline
				auto [v_size, v_align] = get_type_size_align(real_type);
				auto [ut_size, ut_align] = get_type_size_align(elem_type);
				const int field_offset = align_offset(v_align, offset, v_size);
				auto typed_array_name = get_typed_array_name(cft(elem_type)->token_id);
				out << "NPRPC.unmarshal_typed_array(buf, offset + " << field_offset 
				    << ", " << ut_size << ") as " << typed_array_name << ";\n";
				// Convert TypedArray to Array
				out << bl() << result_name << "." << f->name << " = Array.from(" << temp_var << ");\n";
				return;
			}
		}
		// For other aliases (including array aliases), just resolve and unmarshal
		auto temp_field = *f;
		temp_field.type = real_type;
		emit_field_unmarshal(&temp_field, offset, result_name);
		return;
	}
	default:
		assert(false);
	}
}

void TypescriptBuilder::emit_marshal_function(AstStructDecl* s) {
	calc_struct_size_align(s);
	
	out << bl() << "export function marshal_" << s->name << "(buf: NPRPC.FlatBuffer, offset: number, data: " << s->name << "): void {\n";
	bb();
	
	int current_offset = 0;
	for (auto field : s->fields) {
		emit_field_marshal(field, current_offset, "data");
	}
	
	out << eb();
}

void TypescriptBuilder::emit_unmarshal_function(AstStructDecl* s) {
	out << bl() << "export function unmarshal_" << s->name << "(buf: NPRPC.FlatBuffer, offset: number): " << s->name << " {\n";
	bb();
	
	out << bl() << "const result = {} as " << s->name << ";\n";
	
	int current_offset = 0;
	// For exceptions, skip field 0 (__ex_id) as it's implicit and not part of the class
	size_t start_index = s->is_exception() ? 1 : 0;
	for (size_t i = start_index; i < s->fields.size(); ++i) {
		emit_field_unmarshal(s->fields[i], current_offset, "result");
	}
	
	out << bl() << "return result;\n" << eb();
}

TypescriptBuilder::TypescriptBuilder(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir)
	: Builder(ctx)
{
	auto filename = file_path.filename();
	filename.replace_extension(".ts");
	out.open(out_dir / filename, std::ios::binary);

	if (ctx_.is_nprpc_base()) {
		out << "import * as NPRPC from '@/base'\n\n";
	} else if (ctx_.is_nprpc_nameserver()) {
		out << "import * as NPRPC from '@/index_internal'\n\n";
	} else {
		out << "import * as NPRPC from 'nprpc'\n\n";
	}

	out <<
		"const u8enc = new TextEncoder();\n"
		"const u8dec = new TextDecoder();\n\n"
		;

	emit_heap_views();
}
