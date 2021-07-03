#include "ts_builder.hpp"
#include <cassert>
#include <string_view>
#include <algorithm>
#include "utils.hpp"

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
const _token_type token_type;

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
			//os << "Int64(this.dv.getInt32" << '(' << offset << ", true)" << " + this.dv.getInt32" << '(' << offset + 4 << ", true) << 32)";
			break;
		case TokenId::UInt64:
			//os << "UInt64(this.dv.getUint32" << '(' << offset << ", true)" << " + this.dv.getInt32" << '(' << offset + 4 << ", true) << 32)";
			break;
		case TokenId::Float32:
			os << "this.buffer.dv.getFloat32" << "(this.offset+" << offset << ')';
			break;
		case TokenId::Float64:
			os << "this.buffer.dv.getFloat64" << "(this.offset+" << offset << ')';
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
			//os << "Int64(this.dv.setInt32" << '(' << offset << ", true)" << " + this.dv.setInt32" << '(' << offset + 4 << ", true) << 32)";
			break;
		case TokenId::UInt64:
			//os << "UInt64(this.dv.setUint32" << '(' << offset << ", true)" << " + this.dv.setInt32" << '(' << offset + 4 << ", true) << 32)";
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

void Builder_Typescript::emit_type(Ast_Type_Decl* type) {
	/*
	if (type->id == FieldType::Fundamental) {
		out << token_type << type->token_id;
	} else if (type->id == FieldType::Struct) {
		out << type->s->name;
	} else if (type->id == FieldType::Vector) {
		assert(false); // should not happen
	} else if (type->id == FieldType::String) {
		assert(false); // should not happen
	} else if (type->id == FieldType::Array) {
		assert(false); // should not happen
	}
	*/
}

void Builder_Typescript::emit_span_body(Ast_Struct_Decl* s) {
	assert(s->has_span_class == 0);

	spans <<
		"export class Span_" << s->name << " extends IdlUtils.SpanBase {\n"
		"  *[Symbol.iterator]() {\n"
		"    for (let addr = this.first; addr < this.last; addr += this.pod_size) {\n"
		"      yield new " << s->name << "_Direct(this.buffer, addr);\n"
		"    }\n"
		"  }\n"
		"}\n";
}

void Builder_Typescript::emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, int& last_field_ended) {
	/*
	switch (f->type->id) {
	case FieldType::Fundamental: {
		int size = get_fundamental_size(f->type->token_id);
		int offset = align_offset(size, last_field_ended, size);
		
		out << "  public get " << f->name <<
			"() { return " << read_field(offset) << f->type->token_id << "; }\n";

		out << "  public set " << f->name <<
			"(value: number) { " << write_field(offset) << f->type->token_id << "; }\n";

		break;
	}
	case FieldType::Struct: {
		int next = align_offset(f->type->s->align, last_field_ended, f->type->s->size);
		out << "  public get " << f->name << "() { return new "<< f->type->s->name
			<< "_Direct(this.buffer, this.offset + " << next << "); }\n";
		break;
	}
	case FieldType::Array:

		break;
	case FieldType::Vector: {
		const int offset_addr = align_offset(4, last_field_ended, 4);
		const int n_addr = align_offset(4, last_field_ended, 4);
		auto [size, align] = get_field_size_align(f->type->p);
		
		out <<
			"  public set_length_" << f->name << "(n: number) {\n"
			"    IdlUtils._alloc(this.buffer, this.offset + " << offset_addr << ", n, " << size << ", " << align << ");\n"
			"  }\n";
		
		out <<
			"  public get_" << f->name << "() {\n"
			"    const pod_size = " << size << ";\n"
			"    const offset = this.offset + " << offset_addr << " + this.buffer.dv.getUint32(this.offset + " << offset_addr << ", true);\n"
			"    const size = pod_size * this.buffer.dv.getUint32(this.offset + " << n_addr << ", true);\n"
			"    return new " << ((f->type->p->id == FieldType::Fundamental) ? "IdlUtils.Span_" : "Span_"); emit_type(f->type->p); out << "(this.buffer, offset, offset + size, pod_size);\n"
	    "  }\n";

		if (f->type->p->id == FieldType::Struct) {
			if (!f->type->p->s->has_span_class) {
				emit_span_body(f->type->p->s);
				f->type->p->s->has_span_class = true;
			}
		}

		break;
	}
	case FieldType::String: {
		const int offset_addr = align_offset(4, last_field_ended, 4);
		const int n_addr = align_offset(4, last_field_ended, 4);

		out <<
			"  public get_" << f->name << "() {\n"
			"    let enc = new TextDecoder(\"utf-8\");\n"
			"    let v_begin = this.offset + " << offset_addr << ";\n"
			"    let data_offset = v_begin + this.buffer.dv.getUint32(v_begin, true);\n"
			"    let bn = this.buffer.array_buffer.slice(data_offset, data_offset + this.buffer.dv.getUint32(v_begin + 4, true));\n"
			"    return enc.decode(bn);\n"
			"  }\n";

		out <<
			"  public set_" << f->name << "(str: string) {\n"
			"    let enc = new TextEncoder();\n"
			"    let bytes = enc.encode(str);\n"
			"    let len = bytes.length;\n"
			"    let offset = IdlUtils._alloc(this.buffer, this.offset + " << offset_addr << ", len + 4, " << 1 << ", " << 4 << ");\n"
			"    new Uint8Array(this.buffer.array_buffer, offset).set(bytes);\n"
			"  }\n";

		break;
	}
	default:
		assert(false);
		break;
	}
	*/
}


void Builder_Typescript::emit_struct(Ast_Struct_Decl* s) {
	calc_struct_size_align(s);

	auto const accessor_name = s->name + "_Direct";

	out << "export class " << accessor_name << " {\n";

	out << "  private buffer: IdlUtils.NetBuffer;\n";
	out << "  private offset: number;\n\n";

	out << "  " << "constructor(buffer: IdlUtils.NetBuffer, offset: number) {\n"
		"    this.buffer = buffer;\n"
		"    this.offset = offset;\n"
		"  }\n";

	int offset = 0;
	for (auto& f : s->fields) {
		emit_accessors(s->name, f, offset);
	}

	out << "}\n\n";
}

void Builder_Typescript::emit_exception(Ast_Struct_Decl* s) {

}

/*
void Builder_Typescript::emit_msg_class(Ast_MessageDescriptor_Decl* m) {
	out <<
		"export class " << m->name << " {\n"
		"  private buffer: IdlUtils.NetBuffer;\n\n"
		"  public static init() {\n"
		"    let msg = new " <<  m->name << "();\n"
		"    msg.buffer = new IdlUtils.NetBuffer();\n"
		"    msg.buffer.prepare(" << 8 + m->s->size << ");\n"
		"    msg.buffer.commit(" << 8 + m->s->size << ");\n"
		"    msg.buffer.dv.setUint32(0," << m->message_id << ",true);\n"
		"    return msg;\n"
		"  }\n"
		"  public static from_buffer(buffer: IdlUtils.NetBuffer) {\n"
		"    let msg = new " <<  m->name << "();\n"
		"    msg.buffer = buffer;\n"
		"    return msg;\n"
		"  }\n"
		"  public get acc() {\n"
		"    return new " << m->s->name << "_Direct(this.buffer, 8);\n"
		"  }\n"
		"  public send(ws: WebSocket) {\n"
		"    ws.send(this.buffer.array_buffer);\n"
		"  }\n"
		"}\n\n"
		;
}

void Builder_Typescript::emit_message_map(std::vector<Ast_MessageDescriptor_Decl*>& lst) {
	out << "export enum MessageId {\n";
	for (auto msg : lst) out << "  " << msg->name << " = " << msg->message_id << ",\n";
	out << "}\n\n";
}
*/

void Builder_Typescript::emit_handlers(std::vector<Ast_MessageDescriptor_Decl*>& lst) {
	auto const msg_size = lst.size();
	if (!msg_size) return;

	std::vector<std::string> fn_decl;
	std::transform(lst.begin(), lst.end(), std::back_inserter(fn_decl), [](auto m) {
		return "(msg: " + m->name + ") => void"; 
		});

	for (size_t i = 0; i < msg_size; ++i) {
		out << "let g_" << lst[i]->name << "_cb: " << fn_decl[i] << " = null;\n";
	}

	out << "\nexport function init_callbacks(\n";

	for (size_t i = 0; i < msg_size - 1; ++i) {
		out << "  _" << lst[i]->name << "_cb: " << fn_decl[i] << ",\n";
	}

	out << "  _" << lst[msg_size-1]->name << "_cb: " << fn_decl[msg_size-1] << ") {\n";

	for (size_t i = 0; i < msg_size; ++i) {
		out << "  g_" << lst[i]->name << "_cb = _" << lst[i]->name << "_cb;\n";
	}

	out << "}\n\n";

	out << "export function process_message(buffer: IdlUtils.NetBuffer) {\n";
	out << "  switch(buffer.dv.getUint32(0, true)) {\n";

	for (size_t i = 0; i < msg_size; ++i) {
		out << "    case MessageId." << lst[i]->name << ":\n";
		out << "      g_" << lst[i]->name << "_cb(" << lst[i]->name << ".from_buffer(buffer));\n";
		out << "      break;\n";
	}

	out << "    default:\n";
	//out << "      assert(false);\n";
	out << "      break;\n";
	out << "  }\n";
	out << "}\n\n";
}

void Builder_Typescript::emit_file_footer() {
	out << spans.str();
}

void Builder_Typescript::emit_using(Ast_Alias_Decl* u) {

}

void Builder_Typescript::emit_enum(Ast_Enum_Decl* e) {

}

void Builder_Typescript::emit_namespace_begin() {

}

void Builder_Typescript::emit_namespace_end() {

}

void Builder_Typescript::emit_interface(Ast_Interface_Decl* ifs) {

}


Builder_Typescript::Builder_Typescript(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir)
	: Builder(ctx)
	, out(file_path.replace_extension(".ts")) {
	out << "import * as IdlUtils from '../include/npidl/npidl'\n\n";
}