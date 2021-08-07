// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "ts_builder.hpp"
#include <cassert>
#include <string_view>
#include <algorithm>
#include "utils.hpp"

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
static std::ostream& operator << (std::ostream& os, const token_os_mod<_Mod>& field) {
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
		case TokenId::Int8:			os << "I8"; break;
		case TokenId::UInt8:		os << "U8"; break;
		case TokenId::Int16:		os << "I16"; break;
		case TokenId::UInt16:		os << "U16"; break;
		case TokenId::Int32:		os << "I32"; break;
		case TokenId::UInt32:		os << "U32"; break;
		case TokenId::Int64:		os << "I64"; break;
		case TokenId::UInt64:		os << "U64"; break;
		case TokenId::Float32:	os << "Float32"; break;
		case TokenId::Float64:	os << "Float64"; break;
		default: assert(false);
		}
		
	} else {
		assert(false);
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const Builder_Typescript::_ns& ns) {
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


void Builder_Typescript::emit_type(Ast_Type_Decl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << toktype << fundamental_to_ts(cft(type)->token_id) << "/*" << cft(type)->token_id << "*/";
		break;
	case FieldType::Struct:
		os << cflat(type)->name;
		break;
	case FieldType::Vector:
	case FieldType::Array:
		os << "Array<";  emit_type(cwt(type), os); os << ">";
		break;
	case FieldType::String:
		os << "string";
		break;
	case FieldType::Void:
		os << "void";
		break;
	case FieldType::Object:
		os << "nprpc.ObjectId";
		break;
	case FieldType::Alias:
		os << ns(calias(type)->nm) << calias(type)->name;
		break;
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	default:
		assert(false);
	}
}

void Builder_Typescript::emit_flat_type(Ast_Type_Decl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << cft(type)->token_id;
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		os << s->nm->to_ts_namespace() << "Flat_" << ctx_.base_name << '.' << s->name;
		break;
	}
	case FieldType::Vector:
		os << "NPRPC.Flat.Vector<";
		emit_flat_type(cvec(type)->type, os);
		os << ">";
		break;
	case FieldType::String:
		os << "NPRPC.Flat.String";
		break;
	case FieldType::Array:
		os << "NPRPC.Flat.Array<";
		emit_flat_type(car(type)->type, os);
		os << ',' << car(type)->length << '>';
		break;
	case FieldType::Object:
		os << "NPRPC.Detail.Flat.ObjectId";
		break;
	case FieldType::Alias: {
		emit_flat_type(calias(type)->get_real_type(), os);
		break;
	}
	case FieldType::Enum:
		os << ns(cenum(type)->nm) << cenum(type)->name;
		break;
	default:
		assert(false);
	}
}

void Builder_Typescript::emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, int& last_field_ended) {
	switch (f->type->id) {
	case FieldType::Fundamental: 
	case FieldType::Enum:
	{
		const int size = get_fundamental_size(cft(f->type)->token_id);
		const int offset = align_offset(size, last_field_ended, size);

		out << "  public get " << f->name <<
			"() { return " << read_field(offset) << cft(f->type)->token_id << "; }\n";

		if (f->type->id == FieldType::Fundamental) {
			out << "  public set " << f->name <<
			"(value: " << fundamental_to_ts(cft(f->type)->token_id) << ") { " << write_field(offset) << cft(f->type)->token_id << "; }\n";
		} else {
			out << "  public set " << f->name <<
			"(value: " <<  ns(cenum(f->type)->nm) << cenum(f->type)->name << ") { " << write_field(offset) << cft(f->type)->token_id << "; }\n";
		}
		
		break;
	}
	case FieldType::Struct: 
		out << "  public get " << f->name << "() { return new " << cflat(f->type)->name
			<< "_Direct(this.buffer, this.offset + " 
			<< align_offset(cflat(f->type)->align, last_field_ended, cflat(f->type)->size) << "); }\n";
		break;
	
	case FieldType::Vector:
	{
		auto utype = cwt(f->type);
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(utype);
		auto const vec_offset = align_offset(v_align, last_field_ended, v_size);

		// _alloc(buffer: FlatBuffer, vector_offset: number, n: number, element_size: number, align: number)
		out << 
			"  public " << f->name << "(elements_size: number): void { \n"
			"    NPRPC.Flat._alloc(this.buffer, this.offset + " << vec_offset << ", elements_size, " << ut_size << ", " << ut_align << ");\n"
			"  }\n";

		
		for (;;) {
			if (utype->id == FieldType::Fundamental || utype->id == FieldType::Enum) {
				out << 
					"  public " << f->name << "_vd() { return new NPRPC.Flat.Vector_Direct1_" << toktype << cft(utype)->token_id <<
				 "(this.buffer, this.offset + " << vec_offset << "); }\n";
			} else if (utype->id == FieldType::Struct) {
				out << "  public " << f->name << "_vd() { return new NPRPC.Flat.Vector_Direct2<"; emit_flat_type(utype, out); out << "_Direct>"
					"(this.buffer, this.offset + " << vec_offset << ", " << ut_size << ", "; emit_flat_type(utype, out); out << "_Direct); }\n";
			} else if (utype->id == FieldType::Alias) {
				utype = calias(utype)->get_real_type();
				continue;
			} else {
				assert(false);
			}
			break;
		}
		
		break;
	}
	case FieldType::Array:
	{
		auto utype = cwt(f->type);
		auto [v_size, v_align] = get_type_size_align(f->type);
		auto [ut_size, ut_align] = get_type_size_align(utype);
		auto const vec_offset = align_offset(v_align, last_field_ended, v_size);
		auto const arr_length = car(f->type)->length;

		for (;;) {
			if (utype->id == FieldType::Fundamental || utype->id == FieldType::Enum) {
				out << 
					"  public " << f->name << "_vd() { return new NPRPC.Flat.Array_Direct1_" << toktype << cft(utype)->token_id <<
				 "(this.buffer, this.offset + " << vec_offset << ", " << arr_length <<"); }\n";
			} else if (utype->id == FieldType::Struct) {
				out << "  public " << f->name << "_vd() { return new NPRPC.Flat.Array_Direct2<"; emit_flat_type(utype, out); out << "_Direct>"
					"(this.buffer, this.offset + " << vec_offset << ", " << ut_size << ", "; emit_flat_type(utype, out); out << "_Direct" << ", " << arr_length << "); }\n";
			} else if (utype->id == FieldType::Alias) {
				utype = calias(utype)->get_real_type();
				continue;
			} else {
				assert(false);
			}
			break;
		}
		
		break;
	}
	case FieldType::String: {
		const int offset_addr = align_offset(4, last_field_ended, 4);
		const int n_addr = align_offset(4, last_field_ended, 4);

		out <<
			"  public get " << f->name << "() {\n"
			"    let enc = new TextDecoder(\"utf-8\");\n"
			"    let v_begin = this.offset + " << offset_addr << ";\n"
			"    let data_offset = v_begin + this.buffer.dv.getUint32(v_begin, true);\n"
			"    let bn = this.buffer.array_buffer.slice(data_offset, data_offset + this.buffer.dv.getUint32(v_begin + 4, true));\n"
			"    return enc.decode(bn);\n"
			"  }\n";

		out <<
			"  public set " << f->name << "(str: string) {\n"
			"    let enc = new TextEncoder();\n"
			"    let bytes = enc.encode(str);\n"
			"    let len = bytes.length;\n"
			"    let offset = NPRPC.Flat._alloc(this.buffer, this.offset + " << offset_addr << ", len + 4, " << 1 << ", " << 4 << ");\n"
			"    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);\n"
			"  }\n";

		break;
	}
	case FieldType::Object:
		out << "  auto " << f->name << "() noexcept { return "
			"NPRPC.detail.Flat.ObjectId_Direct(this.buffer, this.offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
		break;
	case FieldType::Alias: {
		auto temp = std::make_unique<Ast_Field_Decl>();
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

void Builder_Typescript::emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_ts(cft(type)->token_id);
		break;
	case FieldType::Struct:
		emit_flat_type(type, os); os << "_Direct";
		break;
	case FieldType::Array:
		break;
	case FieldType::Vector: {
		auto utype = cwt(type);

		for (;;) {
			if (utype->id == FieldType::Fundamental || utype->id == FieldType::Enum) {
				out << "NPRPC.Flat.Vector_Direct1_" << toktype << cft(utype)->token_id;
			} else if (utype->id == FieldType::Struct) {
				out << "NPRPC.Flat.Vector_Direct2<"; emit_parameter_type_for_servant_callback_r(utype, out, input); out << ">";
			} else if (utype->id == FieldType::Alias) {
				utype = calias(utype)->get_real_type();
				continue;
			} else {
				assert(false);
			}
			break;
		}

//			for (;;) {
//				if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
//					os << "/*out*/::flat::Vector_Direct1<"; emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
//				} else if (wt->id == FieldType::Struct) {
//					os << "/*out*/::flat::Vector_Direct2<"; emit_parameter_type_for_servant_callback_r(wt, os, input); os << ", ";
//					emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
//				} else if (wt->id == FieldType::Alias) {
//					wt = calias(wt)->get_real_type();
//					continue;
//				} else {
//					assert(false);
//				}
//				break;
//			}
//		}
		break;
	}
	case FieldType::String:
		if (input) {
			os << "::flat::Span<char>";
		} else {
			os << "std::string";
		}
		break;
	case FieldType::Object:
		if (input) {
			os << "nprpc::Object*";
		} else {
			os << "nprpc::detail::flat::ObjectId_Direct";
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

void Builder_Typescript::emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os) {
	auto const input = (arg->modifier == ArgumentModifier::In);
	emit_parameter_type_for_servant_callback_r(arg->type, os, input);
//	if (!input &&
//		(arg->type->id != FieldType::Vector
//			&& arg->type->id != FieldType::Object
//			&& arg->type->id != FieldType::String)) {
//		os << '&';
//	}
}


void Builder_Typescript::emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_ts(cft(type)->token_id);
		break;
	case FieldType::Struct:
		if (auto nm = ctx_.nm_cur()->to_ts_namespace(); !nm.empty()) os << nm << '.';
		os << cflat(type)->name;
		break;
	case FieldType::Array:
	case FieldType::Vector:
		os << "Array<";
		emit_parameter_type_for_proxy_call_r(cwt(type), os, input);
		os << ">";
		if (type->id == FieldType::Array) os << "/*" << car(type)->length << "*/";
		break;
	case FieldType::String:
		os << "string";
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
		if (input) os << "ObjectId";
		else os << "Object*";
		break;
	default:
		assert(false);
	}
}

void Builder_Typescript::emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os) {
	const bool input = (arg->modifier == ArgumentModifier::In);

	os << (input ? "/*in*/" : "/*out*/");
//	if (input && arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector) {
//		os << "const ";
//	}
	const bool as_reference = (arg->modifier == ArgumentModifier::Out && (arg->direct || is_fundamental(arg->type)));
	if (as_reference) os << "NPRPC.ref<";
	if (!input && arg->direct) {
		emit_parameter_type_for_servant_callback_r(arg->type, os, input);
	} else {
		emit_parameter_type_for_proxy_call_r(arg->type, os, input);
	}
	if (as_reference) os << '>';
	//if (!input) {
	//	os << '&';
	//}
}


void Builder_Typescript::assign_from_ts_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator) {
	switch (type->id) {
	case FieldType::Fundamental:
	case FieldType::String:
	case FieldType::Enum:
		out << "  " << op1 << " = " << op2 << ";\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		for (auto field : s->fields) {
			assign_from_ts_type(field->type, op1 + (from_iterator ? "." : ".") + field->name, op2 + '.' + field->name);
		}
		break;
	}
	case FieldType::Vector:
		out << "  " << op1 << '(' << op2 << ".length);\n";
		[[fallthrough]];
	case FieldType::Array: {
		auto utype = cwt(type);
		if (is_fundamental(utype)) {
			auto [size, align] = get_type_size_align(utype);
			out << "  " << op1 << "_vd().copy_from_ts_array(" << op2 << "); \n";
		} else {
			out <<
				"  {\n"
				"  let vv = " << op1 << "_vd(), index = 0;\n"
				"  for (let e of vv) {\n"
				"    "; assign_from_ts_type(utype, "    e", op2 + "[index]", true); out <<
				"    ++index;\n"
				"  }\n"
				"  }\n"
				;
		}
		break;
	}
	case FieldType::Alias:
		assign_from_ts_type(calias(type)->type, op1, op2, from_iterator);
		break;
	case FieldType::Object:
		out << "  memcpy(&" << op1 << "().ip4(), &" << op2 << "._data().ip4, " << size_of_object_without_class_id <<");\n";
		out << "  " << op1 << "().class_id(" << op2 << "._data().class_id);\n";
		break;
	default:
		assert(false);
		break;
	}
}

void Builder_Typescript::assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator, bool top_object, bool direct) {
	static int _idx = 0;
	switch (type->id) {
	case FieldType::Fundamental:
	case FieldType::String:
	case FieldType::Enum:
		out << "  " << op1 << " = " << op2 << ";\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		out << "  (" << op1 << " as any) = new Object();\n";
		for (auto field : s->fields) {
			assign_from_flat_type(field->type, op1 + (from_iterator ? "." : ".") + field->name, op2 + '.' + field->name, false, false);
		}
		break;
	}
	case FieldType::Array:
	case FieldType::Vector:
	{
		auto utype = cwt(type);

		if (top_object && direct) {
			// expecting out passed by reference
			out << "  " << op1 << ".value = " << op2 << "_vd();\n";
			break;
		}

		auto idxs = "index_" + std::to_string(_idx++);
		out <<
			"  {\n"
			"  let vv = " << op2 << "_vd(), " << idxs << " = 0;\n";
		if (top_object) out << "  " << op1 << ".length = vv.elements_size;\n";
		else out << "  (" << op1 << " as Array<any>) = new Array<any>(vv.elements_size)\n";
		out <<
			"  for (let e of vv) {\n";
		if (!is_fundamental(utype)) out << "    (" << op1 << '[' << idxs << "] as any) = new Object();\n";
		assign_from_flat_type(utype, op1 + '[' + idxs + ']', "e", true, false);
		out <<
			"    ++" << idxs << ";\n"
			"  }\n"
			"  }\n"
			;
		break;
	}
	case FieldType::Alias:
		assign_from_flat_type(calias(type)->get_real_type(), op1, op2, from_iterator, false);
		break;
	case FieldType::Object:
		out << "  " << op1 << " = this->create_from_object_id(" << op2 << "());\n";
		break;
	default:
		assert(false);
		break;
	}
}

void Builder_Typescript::emit_struct2(Ast_Struct_Decl* s, bool is_exception) {
	calc_struct_size_align(s);

	// native typescript
	if (!is_exception) {
		out << "export interface " << s->name << " {\n";
		for (auto const f : s->fields) {
			out << "  " << f->name << ": "; emit_type(f->type, out); out << ";\n";
		}
	} else {
		out <<
			"export class " << s->name << " extends NPRPC.Exception {\n"
			"  constructor(public ";
		for (size_t ix = 1; ix < s->fields.size(); ++ix) {
			auto f = s->fields[ix];
			out << f->name << "?: "; emit_type(f->type, out);
			if (ix + 1 < s->fields.size()) out << ", ";
		}
		out << ") { super(\""<< s->name << "\"); }\n";
	}

	out << "}\n\n";

	// flat type

	auto const accessor_name = s->name + "_Direct";

	out << "export namespace Flat_" << ctx_.base_name << " {\n";
	out << "export class " << accessor_name << " extends NPRPC.Flat.Flat {\n";

	int offset = 0;
	for (auto& f : s->fields) {
		emit_accessors(s->name, f, offset);
	}

	out << "}\n} // namespace Flat \n";
}

void Builder_Typescript::emit_struct(Ast_Struct_Decl* s) {
	emit_struct2(s, false);
}

void Builder_Typescript::emit_exception(Ast_Struct_Decl* s) {
	assert(s->is_exception());
	emit_struct2(s, true);
}

void Builder_Typescript::emit_file_footer() {
	// throw_exception function body
	auto& exs = ctx_.exceptions;
	if (!exs.empty()) {

		out <<
			"\n"

			"function " << ctx_.base_name << "_throw_exception(buf: NPRPC.FlatBuffer): void { \n"
			"  switch( buf.read_exception_number() ) {\n"
			;

		always_full_namespace(true);
		for (auto ex : exs) {
			out <<
				"  case " << ex->exception_id << ":\n"
				"  {\n"
				"    let ex_flat = new Flat_" << ctx_.base_name << '.' << ns(ex->nm) << ex->name << "_Direct(buf, " << size_of_header << ");\n"
				"    let ex = new " << ns(ex->nm) << ex->name << "();\n"
				;

			for (size_t i = 1; i < ex->fields.size(); ++i) {
				auto f = ex->fields[i];
				assign_from_flat_type(f->type, "ex." + f->name, "ex_flat." + f->name);
			}


			out <<
				"    throw ex;\n"
				"  }\n"
				;
		}
		always_full_namespace(false);


		out <<
			"  default:\n"
			"    throw \"unknown rpc exception\";\n"
			"  }\n"
			"}\n"
			;
	}

	// other

	emit_arguments_structs(std::bind(&Builder_Typescript::emit_struct2, this, _1, false));
}

void Builder_Typescript::emit_using(Ast_Alias_Decl* u) {
	out << "export type " << u->name << " = "; emit_type(u->type, out); out << ";\n";
}

void Builder_Typescript::emit_enum(Ast_Enum_Decl* e) {
	out << "export const enum " << e->name << " { //" << toktype << e->token_id << '\n';
	for (size_t i = 0, ix = 0; i < e->items.size(); ++i) {
		out << "  " << e->items[i].first;
		auto const n = e->items[i].second;
		if (ix != n) {
			out << " = " << n;
			ix = n + 1;
		} else {
			++ix;
		}
		if (i != e->items.size() - 1) out << ",\n";
	}
	out << "\n}\n";
}

void Builder_Typescript::emit_namespace_begin() {
	if (ctx_.nm_cur()->parent() && ctx_.nm_cur()->parent()->name().empty()) return;
	out << "export namespace " << ctx_.nm_cur()->name() << " { \n";
}

void Builder_Typescript::emit_namespace_end() {
	if (ctx_.nm_cur()->parent() && ctx_.nm_cur()->parent()->name().empty()) return;
	out << "} // namespace " << ctx_.nm_cur()->name() << "\n\n";
}

void Builder_Typescript::emit_interface(Ast_Interface_Decl* ifs) {
	auto emit_function_arguments = [](Ast_Function_Decl* fn, std::ostream& os,
		std::function<void(Ast_Function_Argument*, std::ostream& os)> emitter) {
			os << "(";
			size_t ix = 0;
			for (auto arg : fn->args) {
				os << arg->name << ": "; 
				emitter(arg, os);
				if (++ix != fn->args.size()) os << ", ";
			}
			os << ')';
	};


	// Proxy definition =======================================================
	out <<
		"export class " << ifs->name;

	if (ifs->plist.size()) {
		out << "  : public " << ifs->plist[0]->name << "\n";
		for (size_t i = 1; i < ifs->plist.size(); ++i) {
			out << "  , public " << ifs->plist[i]->name << "\n";
		}
		out << "{\n";
	} else {
		out << " extends NPRPC.ObjectProxy\n{\n";
	}

	out <<
		"  public interface_idx: number;\n"
//		"  type servant_t = I" << ifs->name << "_Servant;\n\n"
		;

	if (ifs->plist.empty()) {
		out << "  constructor (interface_idx: number) { super(); this.interface_idx = interface_idx; }\n";
	} else {
		out <<
			"  " << ifs->name << "(uint8_t interface_idx)\n"
			"    : interface_idx_(interface_idx)\n"
			;

		auto count_all = [](Ast_Interface_Decl* ifs_inherited, int& n) { ++n; };

		int n = 1;
		for (auto parent : ifs->plist) {
			out <<
				"    , " << parent->name << "(interface_idx + " << n << ")\n"
				;
			dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
		}
		out <<
			"  {\n"
			"  }\n"
			;
	}

	// proxy object functions definitions
	out << '\n';
	for (auto& fn : ifs->fns) {
		make_arguments_structs(fn);
		out << "  public async " << fn->name;
		emit_function_arguments(fn, out,
			std::bind(&Builder_Typescript::emit_parameter_type_for_proxy_call, this, _1, _2)
		);
		out << ": Promise<"; emit_type(fn->ret_value, out);  out << "> {\n";

		const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
		const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);
		out << 
			"    let buf = NPRPC.FlatBuffer.create();\n"
			"    buf.prepare(" << capacity << ");\n"
			"    buf.commit(" << fixed_size << ");\n"
			"    buf.write_msg_id(NPRPC.impl.MessageId.FunctionCall);\n"
			"    buf.write_msg_type(NPRPC.impl.MessageType.Request);\n"
			"    let __ch = new NPRPC.impl.Flat_nprpc_base.CallHeader_Direct(buf, " << size_of_header << ");\n"
			"    __ch.object_id = this.data.object_id;\n"
			"    __ch.poa_idx = this.data.poa_idx;\n"
			"    __ch.interface_idx = this.interface_idx;\n"
			"    __ch.function_idx = " << fn->idx << ";\n"
			;

		if (fn->in_s) {
			out <<
				"  let _ = new Flat_" << ctx_.base_name << '.' << fn->in_s->name << "_Direct(buf," << get_arguments_offset() << ");\n"
				;
		}

		int ix = 0;
		for (auto in : fn->args) {
			if (in->modifier == ArgumentModifier::Out) continue;
			assign_from_ts_type(in->type, "_._" + std::to_string(++ix), in->name);
		}

		out << 
			"    buf.write_len(buf.size - 4);\n";

		out <<
			"    await NPRPC.g.rpc.call(\n"
			"      {ip4: this.data.ip4, port: this.data.port}, buf, this.timeout\n"
			"    );\n"
			"    let std_reply = NPRPC.handle_standart_reply(buf);\n"
			;

		if (fn->ex) {
			out <<
				"    if (std_reply == 1) {\n"
				"      " << ctx_.base_name << "_throw_exception(buf);\n"
				"    }\n"
				;
		}

		if (!fn->out_s) {
			out <<
				"    if (std_reply != 0) {\n"
				"      console.log(\"received an unusual reply for function with no output arguments\");\n"
				"    }\n"
				;
		} else {
			out <<
				"    if (std_reply != -1) {\n"
				"      console.log(\"received an unusual reply for function with output arguments\");\n"
				"      throw new NPRPC.Exception(\"Unknown Error\");\n"
				"    }\n"
				;

			// (sizeof the message + sizeof BlockResponse) next struct will be always aligned by 8
			out << "  let out = new Flat_" << ctx_.base_name << '.' << fn->out_s->name << "_Direct(buf, " << size_of_header << ");\n";

			int ix = fn->is_void() ? 0 : 1;

			for (auto out : fn->args) {
				if (out->modifier == ArgumentModifier::In) continue;
				assign_from_flat_type(out->type, out->name + (is_fundamental(out->type) ? ".value" : ""), "out._" + std::to_string(++ix), false, true, out->direct);
			}

			if (!fn->is_void()) {
				out << "  let __ret_value: "; emit_type(fn->ret_value, out); out << ";\n";
				assign_from_flat_type(fn->ret_value, "__ret_value", "out._1");
				out << "  return __ret_value;\n";
			}
		}

		out << "}\n\n";
	}

	out << "};\n\n"; // Proxy class ends

	// Servant definition
//	out << "export abstract class I" << ifs->name << "_Servant\n";
//	if (ifs->plist.size()) {
//		//out << "  : public I" << ifs->plist[0]->name << "_Servant\n";
//		//for (size_t i = 1; i < ifs->plist.size(); ++i) {
//		//	out << "  , public I" << ifs->plist[i]->name << "_Servant\n";
//		//}
//		//out << "{\n";
//	} else {
//		out << "  extends NPRPC.ObjectServant\n{\n";
//	}
//
//	out << "  // Interfaces\n";
//	for (auto fn : ifs->fns) {
//		make_arguments_structs(fn);
//
//		out << "  abstract " << fn->name;
//		emit_function_arguments(fn, out,
//			std::bind(&Builder_Typescript::emit_parameter_type_for_servant_callback, this, _1, _2)
//		);
//		out << ": "; emit_type(fn->ret_value, out); out << ";\n";
//	}
//
//	out <<
//		"\n"
//		"  public static _get_class(): string { return \"" << ctx_.base_name << '/' << ctx_.nm_cur()->to_interface_path() << '.' << ifs->name << "\"; }\n"
//		"  public get_class(): string { return I" << ifs->name << "_Servant._get_class(); }\n"
//		"  public dispatch(bufs: NPRPC.Buffers, remote_endpoint: NPRPC.EndPoint, from_parent: boolean, ref_list: NPRPC.ReferenceList): void {\n"
//		;
//
//	
//
//	// Servant dispatch ====================================================
//	out << 
//		"  let __ch = new NPRPC.impl.flat.CallHeader_Direct(bufs.$, " << size_of_header << ");\n"
//		;
//		
//	if (ifs->plist.empty()) {
//		// ok
//	} else {
//		out <<
//			"  if (from_parent == false) {\n"
//			"    switch(__ch.interface_idx) {\n"
//			"      case 0:\n"
//			"        break;\n"
//			;
//	
//		int ix = 1;
//		auto select_interface = [&ix, this, ifs](Ast_Interface_Decl* i) {
//			if (i == ifs) return;
//			out <<
//				"      case "<< ix << ":\n"
//				"        I" << i->name << "_Servant.dispatch(bufs, remote_endpoint, true, ref_list);\n"
//				"        return;\n"
//			;
//			++ix;
//		};
//		
//		//dfs(select_interface, ifs);
//		
//		out <<
//			"      default:\n"
//			"        assert(false);\n"
//			"        throw \"unknown interface\";\n"
//			"    }\n"
//			"  }\n"
//			;
//	}
//
//	out <<
//			"  switch(__ch.function_idx) {\n"
//			;
//
//
//	for (auto fn : ifs->fns) {
//		out << "    case " << fn->idx << ": {\n";
//
//		int out_ix = fn->is_void() ? 0 : 1;
//
//		if (fn->out_s && fn->out_s->flat) {
//			for (auto arg : fn->args) {
//				if (arg->modifier == ArgumentModifier::Out) {
//					out << "      "; emit_type(arg->type, out); out << " _out_" << ++out_ix << ";\n";
//				}
//			}
//		}
//
//		if (fn->in_s) {
//			out <<
//				"      flat." << fn->in_s->name << "_Direct ia(bufs(), " << get_arguments_offset() << ");\n"
//				;
//		}
//
//		if (fn->out_s && !fn->out_s->flat) {
//			const auto offset = size_of_header;
//			const auto initial_size = offset + fn->out_s->size;
//			out <<
//				"      auto& obuf = bufs.flip();\n"
//				"      obuf.consume(obuf.size());\n"
//				"      obuf.prepare(" << initial_size + 128 << ");\n"
//				"      obuf.commit(" << initial_size << ");\n"
//				"      ::flat::" << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
//				;
//		}
//
//		if (!fn->is_void()) {
//			emit_type(fn->ret_value, out); out << " __ret_val;\n";
//		}
//
//		if (fn->ex) out << "      try {\n";
//		
//		out <<
//			(fn->is_void() ? "      " : "      __ret_val = ") << "this." << fn->name << "("
//			;
//
//		int in_ix = 0, idx = 0; out_ix = fn->is_void() ? 0 : 1;
//		for (auto arg : fn->args) {
//			if (arg->modifier == ArgumentModifier::Out) {
//				assert(fn->out_s);
//				if (!fn->out_s->flat) {
//					out << "oa._" << ++out_ix;
//					if (arg->type->id == FieldType::Vector || arg->type->id == FieldType::String) {
//						out << "_vd()";
//					} else {
//						out << "()";
//					}
//				} else {
//					out << "_out_" << ++out_ix;
//				}
//			} else {
//				if (arg->type->id == FieldType::Object) {
//					out << "NPRPC.impl.g_orb->create_object_from_flat(ia._" << ++in_ix << "(), remote_endpoint)";
//				} else {
//					out << "ia._" << ++in_ix << "()";
//				}
//			}
//			if (++idx != fn->args.size()) out << ", ";
//		}
//		out << ");\n";
//
//		/*
//		out_ix = fn->is_void() ? 0 : 1;
//		for (auto arg : fn->args) {
//			if (arg->modifier == ArgumentModifier::Out) {
//				++out_ix;
//				if (arg->type->id == FieldType::Object) {
//					oc <<
//						"{\n"
//						"  auto obj = impl::g_orb->get_object(" << "oa._" << out_ix << "().poa_idx(), " << "oa._" << out_ix << "().object_id());\n"
//						"  if (obj) if (auto real_obj = (*obj).get(); real_obj) ref_list.add_ref(real_obj);\n"
//						"}\n"
//						;
//				}
//			}
//		}
//		*/
//
//		if (fn->ex) {
//			const auto offset = size_of_header;
//			const auto initial_size = offset + fn->ex->size;
//			
//			always_full_namespace(true);
//			out <<
//				"      }\n"
//				"      catch(" << ns(fn->ex->nm) << fn->ex->name << "& e) {\n"
//				"        auto& obuf = bufs();\n"
//				"        obuf.consume(obuf.size());\n"
//				"        obuf.prepare(" << initial_size << ");\n"
//				"        obuf.commit(" << initial_size << ");\n"
//				"        "<< ns(fn->ex->nm) << "flat::" <<fn->ex->name << "_Direct oa(obuf," << offset << ");\n"
//				"        oa.__ex_id() = " << fn->ex->exception_id << ";\n"
//				;
//			always_full_namespace(false);
//
//			for (auto i = 1; i < fn->ex->fields.size(); ++i) {
//				auto mb = fn->ex->fields[i];
//				//assign_from_cpp_type(mb->type, "oa." + mb->name, "e." + mb->name);
//			}
//
//			out << 
//				"        *((uint32_t*)obuf.data().data()) = static_cast<uint32_t>(obuf.size() - 4);\n"
//				"        *((::nprpc::impl::MessageId*)obuf.data().data() + 1) = ::nprpc::impl::MessageId::Exception;\n"
//				"        return;\n"
//				"      }\n"
//				;
//		}
//
//		if (!fn->out_s) {
//			out << "      NPRPC.make_simple_answer(bufs.$, NPRPC.impl.MessageId.Success);\n";
//		} else {
//			if (fn->out_s->flat) { // it means that we are writing output data in the input buffer, 
//				// so we must pass stack variables first and then assign result back to the buffer
//				const auto offset = size_of_header;
//				const auto initial_size = offset + fn->out_s->size;
//
//				out <<
//					"      auto& obuf = bufs();\n"
//					"      obuf.consume(obuf.size());\n"
//					"      obuf.prepare(" << initial_size << ");\n"
//					"      obuf.commit(" << initial_size << ");\n"
//					"      ::flat::" << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
//					;
//				
//				int ix;
//				if (!fn->is_void()) {
//					//assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val");
//					ix = 1;
//				} else {
//					ix = 0;
//				}
//
//				for (auto out : fn->args) {
//					if (out->modifier == ArgumentModifier::In) continue;
//					auto n = std::to_string(++ix);
//					//assign_from_cpp_type(out->type, "oa._" + n, "_out_" + n);
//				}
//			} else if (!fn->is_void()) {
//				//assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val");
//			}
//
//			out <<
//				"      *((uint32_t*)obuf.data().data()) = static_cast<uint32_t>(obuf.size() - 4);\n"
//				"      *((::nprpc::impl::MessageId*)obuf.data().data() + 1) = NPRPC.impl.MessageId.BlockResponse;\n"
//				;
//		}
//
//		out <<
//			"      break;\n"
//			"    }\n"
//			;
//	}
//
//	out <<
//		"    default:\n"
//		"      NPRPC.make_simple_answer(bufs.$, NPRPC.impl.MessageId.Error_UnknownFunctionIdx);\n"
//		"  }\n"; // switch block
//	;
//
//	out << "  }\n"; // dispatch
//	out << "}\n\n"; // class ends
}

Builder_Typescript::_ns Builder_Typescript::ns(Namespace* nm) {
	return { *this, nm };
}

Builder_Typescript::Builder_Typescript(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir)
	: Builder(ctx)
{
	auto filename = file_path.filename();
	filename.replace_extension(".ts");
	out.open(out_dir / filename);

	if (!ctx_.is_nprpc_base()) {
		out << "import * as NPRPC from './nprpc'\n\n";
	} else {
		out << "import * as NPRPC from './nprpc'\n\n";
	}

}