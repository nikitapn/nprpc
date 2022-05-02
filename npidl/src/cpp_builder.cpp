// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include "cpp_builder.hpp"
#include <cassert>
#include <string_view>
#include <algorithm>
#include <sstream>
#include "utils.hpp"
#include <iostream>
#include <stack>
#include <boost/container/small_vector.hpp>

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;
using std::placeholders::_4;

void Builder::emit_arguments_structs(std::function<void(Ast_Struct_Decl*)> emitter) {
	always_full_namespace(true);
	for (auto& [unused, s] : ctx_.affa_list) emitter(s);
	always_full_namespace(false);
}

void Builder::make_arguments_structs(Ast_Function_Decl* fn) {
	if (fn->arguments_structs_have_been_made) return;

	std::vector<Ast_Function_Argument*> in_args, out_args;

	if (!fn->is_void()) {
		auto ret = new Ast_Function_Argument();
		ret->name = "ret_val";
		ret->modifier = ArgumentModifier::Out;
		ret->type = fn->ret_value;
		out_args.push_back(ret);
	}

	for (auto arg : fn->args) {
		if (arg->modifier == ArgumentModifier::In) in_args.push_back(arg);
		else out_args.push_back(arg);
	}

	auto make_struct = [this, fn](std::vector<Ast_Function_Argument*>& args) -> Ast_Struct_Decl* {
		if (args.empty()) return nullptr;

		auto s = new Ast_Struct_Decl();
		s->name = ctx_.base_name + "_M" + std::to_string(++ctx_.m_struct_n_);
		

		std::transform(args.begin(), args.end(), std::back_inserter(s->fields), [ix = 0, s, fn](Ast_Function_Argument* arg) mutable {
			auto f = new Ast_Field_Decl();
			f->name = "_" + std::to_string(++ix);
			f->type = arg->type;
			s->flat &= is_flat(arg->type);
			f->function_argument = true;
			f->input_function_argument = (arg->modifier == ArgumentModifier::In);
			f->function_name = fn->name;
			f->function_argument_name = arg->name;
			return f;
		});

		calc_struct_size_align(s);
		auto const id = get_function_struct_id(s);

		if (auto p = ctx_.affa_list.get(id); p) {
			--ctx_.m_struct_n_;
			delete s;
			s = p;
		} else {
			ctx_.affa_list.put(id, s);
		}

		return s;
	};

	fn->in_s = make_struct(in_args);
	fn->out_s = make_struct(out_args);

	fn->arguments_structs_have_been_made = true;
}

std::ostream& operator<<(std::ostream& os, const Builder_Cpp::_ns& ns) {
	if (ns.bulder_.always_full_namespace_ || ns.nm != ns.bulder_.ctx_.nm_cur()) {
		os << ns.nm->to_cpp17_namespace() << "::";
	}
	return os;
}

static std::string_view fundamental_to_cpp(TokenId id) {
	using namespace std::string_view_literals;
	switch (id) {
	case TokenId::Boolean: return "bool"sv;
	case TokenId::Int8: return "int8_t"sv;
	case TokenId::UInt8: return "uint8_t"sv;
	case TokenId::Int16: return "int16_t"sv;
	case TokenId::UInt16: return "uint16_t"sv;
	case TokenId::Int32: return "int32_t"sv;
	case TokenId::UInt32: return "uint32_t"sv;
	case TokenId::Int64: return "int64_t"sv;
	case TokenId::UInt64: return "uint64_t"sv;
	case TokenId::Float32: return "float"sv;
	case TokenId::Float64: return "double"sv;
	default: assert(false); return ""sv;
	}
}

void Builder_Cpp::emit_type(Ast_Type_Decl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_cpp(cft(type)->token_id);
		break;
	case FieldType::Struct:
		os << ns(cflat(type)->nm) << cflat(type)->name;
		break;
	case FieldType::Vector:
		os << "std::vector<";
		emit_type(cvec(type)->type, os);
		os << ">";
		break;
	case FieldType::String:
		os << "std::string";
		break;
	case FieldType::Array:
		os << "std::array<";
		emit_type(car(type)->type, os);
		os << ',' << car(type)->length << '>';
		break;
	case FieldType::Void:
		os << "void";
		break;
	case FieldType::Object:
		os << "nprpc::ObjectId";
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

void Builder_Cpp::emit_flat_type(Ast_Type_Decl* type, std::ostream& os) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_cpp(cft(type)->token_id);
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		os << s->nm->to_cpp17_namespace() << "::flat::" << s->name;
		break;
	}
	case FieldType::Vector:
		os << "::flat::Vector<";
		emit_flat_type(cvec(type)->type, os);
		os << ">";
		break;
	case FieldType::String:
		os << "::flat::String";
		break;
	case FieldType::Array:
		os << "::flat::Array<";
		emit_flat_type(car(type)->type, os);
		os << ',' << car(type)->length << '>';
		break;
	case FieldType::Object:
		os << "nprpc::detail::flat::ObjectId";
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

Builder_Cpp::_ns Builder_Cpp::ns(Namespace* nm) {
	return { *this, nm };
}

void Builder_Cpp::emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_cpp(cft(type)->token_id);
		break;
	case FieldType::Struct:
		os << ctx_.nm_cur()->to_cpp17_namespace() << "::" << cflat(type)->name;
		break;
	case FieldType::Vector:
		if (input) {
			os << "::flat::Span<const ";
			emit_parameter_type_for_proxy_call_r(cvec(type)->type, os, input);
			os << ">";
		} else {
			os << "std::vector<";
			emit_parameter_type_for_proxy_call_r(cvec(type)->type, os, input);
			os << ">";
		}
		break;
	case FieldType::String:
		os << "std::string";
		break;
	case FieldType::Array:
		os << "std::array<";
		emit_parameter_type_for_proxy_call_r(car(type)->type, os, input);
		os << ',' << car(type)->length << '>';
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

void Builder_Cpp::emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os) {
	const bool input = (arg->modifier == ArgumentModifier::In);

	os << (input ? "/*in*/" : "/*out*/");
	if (input && arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector) {
		os << "const ";
	}

	emit_parameter_type_for_proxy_call_r(arg->type, os, input);

	if (!input || (arg->type->id != FieldType::Fundamental && arg->type->id != FieldType::Vector)) {
		os << '&';
	}
}

void Builder_Cpp::emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input) {
	switch (type->id) {
	case FieldType::Fundamental:
		os << fundamental_to_cpp(cft(type)->token_id);
		break;
	case FieldType::Struct:
		emit_flat_type(type, os); os << "_Direct";
		break;
	case FieldType::Array:
	case FieldType::Vector: {
		auto wt = static_cast<Ast_Wrap_Type*>(type)->type;
		if (input || type->id == FieldType::Array) {
			for (;;) {
				if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
					os << "::flat::Span<"; emit_flat_type(wt, os); os << ">";
				} else if (wt->id == FieldType::Struct) {
					os << "::flat::Span_ref<"; emit_flat_type(wt, os); os << ", "; emit_flat_type(wt, os); os << "_Direct>";
				} else if (wt->id == FieldType::Alias) {
					wt = calias(wt)->get_real_type();
					continue;
				} else {
					assert(false);
				}
				break;
			}
		} else {
			for (;;) {
				if (wt->id == FieldType::Fundamental || wt->id == FieldType::Enum) {
					os << "/*out*/::flat::Vector_Direct1<"; emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
				} else if (wt->id == FieldType::Struct) {
					os << "/*out*/::flat::Vector_Direct2<"; emit_flat_type(wt, os); os << ", ";
					emit_parameter_type_for_servant_callback_r(wt, os, input); os << ">";
				} else if (wt->id == FieldType::Alias) {
					wt = calias(wt)->get_real_type();
					continue;
				} else {
					assert(false);
				}
				break;
			}
		}
		break;
	}
	case FieldType::String:
		if (input) {
			os << "::flat::Span<char>";
		} else {
			os << "::flat::String_Direct1";
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

void Builder_Cpp::emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os) {
	auto const input = (arg->modifier == ArgumentModifier::In);
	emit_parameter_type_for_servant_callback_r(arg->type, os, input);
	if (!input &&
		(arg->type->id != FieldType::Vector
			&& arg->type->id != FieldType::Object
			&& arg->type->id != FieldType::String)) {
		os << '&';
	}
}

void Builder_Cpp::emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, std::ostream& os) {
	switch (f->type->id) {
	case FieldType::Fundamental: {
		auto type_name = fundamental_to_cpp(cft(f->type)->token_id);
		os << "  const " << type_name << "& " << f->name << "() const noexcept { return base()."
			<< f->name << ";}\n";
		os << "  " << type_name << "& " << f->name << "() noexcept { return base()."
			<< f->name << ";}\n";
		break;
	}
	case FieldType::Struct: {
		auto s = cflat(f->type);
		os << "  auto " << f->name << "() noexcept { return " << s->nm->to_cpp17_namespace() << "::flat::" << s->name
			<< "_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
		break;
	}
	case FieldType::Vector: {
		auto t = static_cast<Ast_Wrap_Type*>(f->type)->type;
		os << "  void " << f->name << "(size_t elements_size) { new (&base()." << f->name << ") ::flat::Vector<";
		emit_flat_type(static_cast<Ast_Wrap_Type*>(f->type)->type, os); os << ">(buffer_, elements_size); }\n";

		for (;;) {
			if (t->id == FieldType::Fundamental || t->id == FieldType::Enum) {
				os << "  auto " << f->name << "_vd() noexcept { return ::flat::Vector_Direct1<";
				emit_flat_type(t, os); os << ">(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
			} else if (t->id == FieldType::Struct) {
				os << "  auto " << f->name << "_vd() noexcept { return ::flat::Vector_Direct2<";
				emit_flat_type(t, os); os << ", "; emit_flat_type(t, os); os << "_Direct>(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
			} else if (t->id == FieldType::Alias) {
				t = calias(t)->get_real_type();
				continue;
			} else {
				assert(false);
			}
			break;
		}
	}
	[[fallthrough]];
	case FieldType::Array: {
		auto t = static_cast<Ast_Wrap_Type*>(f->type)->type;
		for (;;) {
			if (t->id == FieldType::Fundamental || t->id == FieldType::Enum) {
				os << "  auto " << f->name << "() noexcept { return (::flat::Span<";
				emit_flat_type(t, os); os << ">)base()." << f->name << "; }\n";
			} else if (t->id == FieldType::Struct) {
				os << "  auto " << f->name << "() noexcept { return ::flat::Span_ref<";
				emit_flat_type(t, os); os << ", "; emit_flat_type(t, os); os << "_Direct"; os << ">(buffer_, base()." << f->name << ".range(buffer_.data().data())); }\n";
			} else if (t->id == FieldType::Alias) {
				t = calias(t)->get_real_type();
				continue;
			} else {
				assert(false);
			}
			break;
		}
		break;
	}
	case FieldType::String:
		os << "  void " << f->name << "(const char* str) { new (&base()." << f->name << ") ::flat::String(buffer_, str); }\n";
		os << "  void " << f->name << "(const std::string& str) { new (&base()." << f->name << ") ::flat::String(buffer_, str); }\n";
		os << "  auto " << f->name << "() noexcept { return (::flat::Span<char>)base()." << f->name << "; }\n";
		os << "  auto " << f->name << "() const noexcept { return (::flat::Span<const char>)base()." << f->name << "; }\n";
		os << 
			"  auto " << f->name << "_vd() noexcept { "
			"    return ::flat::String_Direct1(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << "));"
			"  }\n";
		break;
	case FieldType::Object:
		os << "  auto " << f->name << "() noexcept { return "
			"nprpc::detail::flat::ObjectId_Direct(buffer_, offset_ + offsetof(" << flat_name << ", " << f->name << ")); }\n";
		break;
	case FieldType::Alias: {
		auto temp = std::make_unique<Ast_Field_Decl>();
		temp->name = f->name;
		temp->type = calias(f->type)->get_real_type();
		emit_accessors(flat_name, temp.get(), os);
		break;
	}
	case FieldType::Enum: {
		auto e = cenum(f->type);
		os << "  const " << ns(e->nm) << e->name << "& " << f->name << "() const noexcept { return base()."
			<< f->name << ";}\n";
		os << "  " << ns(e->nm) << e->name << "& " << f->name << "() noexcept { return base()."
			<< f->name << ";}\n";
		break;
	}
	default:
		assert(false);
		break;
	}
}

void Builder_Cpp::assign_from_cpp_type(Ast_Type_Decl* type, std::string op1, std::string op2, std::ostream& os, bool from_iterator, bool top_type) {
	switch (type->id) {
	case FieldType::Fundamental:
		assert(top_type == false);
		os << "  " << op1 << "() = " << op2 << ";\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		if (s->flat) {
			os << "  memcpy(" << op1 << "().__data(), &" << op2 << ", " << s->size << ");\n";
		} else {
			for (auto field : s->fields) {
				assign_from_cpp_type(field->type, 
					op1 + (top_type ? "." : (from_iterator ? "." : "().")) + field->name, 
					op2 + (from_iterator ? "->" : ".") + field->name, 
					os);
			}
		}
		break;
	}
	case FieldType::Vector:
		if (top_type) {
			os << "  " << op1 << ".length(" << op2 << ".size());\n";
		} else {
			os << "  " << op1 << '(' << op2 << ".size());\n";
		}
		[[fallthrough]];
	case FieldType::Array: {
		auto utype = static_cast<Ast_Wrap_Type*>(type)->type;
		if (is_flat(utype)) {
			auto [size, align] = get_type_size_align(utype);
			os << "  memcpy(" << op1 << "().data(), " << op2 << ".data(), " << op2 << ".size() * " << size << ");\n";
		} else {
			os <<
				"  auto span = " << op1 << "();\n"
				"  auto it = " << op2 << ".begin();\n"
				"  for (auto e : span) {\n"
				"    auto __ptr = ::nprpc::make_wrapper1(*it);\n"
				;

			assign_from_cpp_type(utype, "  e", "__ptr", os, true);

			os << "    ++it;\n"
				"  }\n";

		}
		break;
	}
	case FieldType::Enum:
		assert(top_type == false);
		os << "  " << op1 << "() = " << op2 << ";\n";
		break;
	case FieldType::Alias:
		assert(top_type == false);
		assign_from_cpp_type(calias(type)->type, op1, op2, os, from_iterator);
		break;
	case FieldType::String:
		assert(top_type == false);
		os << "  " << op1 << '(' << op2 << ");\n";
		break;
	case FieldType::Object:
		assert(top_type == false);
		os << "  memcpy(" << op1 << "().__data(), &" << op2 << "._data(), " << size_of_object_without_class_id <<");\n";
		os << "  " << op1 << "().class_id(" << op2 << "._data().class_id);\n";
		break;
	default:
		assert(false);
		break;
	}
}

void Builder_Cpp::assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator, bool top_object) {
	switch (type->id) {
	case FieldType::Fundamental:
		oc << "  " << op1 << " = " << op2 << "();\n";
		break;
	case FieldType::Struct: {
		auto s = cflat(type);
		if (s->flat) {
			oc << "  memcpy(&" << op1 << ", " << op2 << "().__data(), " << s->size << ");\n";
		} else {
			for (auto field : s->fields) {
				assign_from_flat_type(field->type, op1 + '.' + field->name, op2 + (from_iterator ? "." : "().") + field->name);
			}
		}
		break;
	}
	case FieldType::Array:
	case FieldType::Vector:
	{
		auto const size = std::get<0>(get_type_size_align(static_cast<Ast_Wrap_Type*>(type)->type));
		oc <<
			"  {\n"
			"    auto span = " << op2 << "();\n"
			;
		if (type->id == FieldType::Vector) {
			oc <<
				"    " << op1 << ".resize(span.size());\n"
				;
		}
		if (is_flat(static_cast<Ast_Wrap_Type*>(type)->type)) {
			oc <<
				"    memcpy(" << op1 << ".data(), span.data(), " << size << " * span.size());\n"
				;
		} else {
			//oc <<
			//	"    memcpy(" << op1 << ".data(), span.data(), " << size << " * span.size());\n"
			//	;
		}
		oc << "  }\n";
		break;
	}
	case FieldType::String:
		oc << "  " << op1 << " = (std::string_view)" << op2 << "();\n";
		break;
	case FieldType::Enum:
		oc << "  " << op1 << " = " << op2 << "();\n";
		break;
	case FieldType::Alias:
		assign_from_flat_type(calias(type)->get_real_type(), op1, op2, from_iterator);
		break;
	case FieldType::Object:
		if (top_object) {
			oc << "  " << op1 << " = this->create_from_object_id(" << op2 << "());\n";
		} else {
			oc << "  " << op1 << ".assign_from_direct(" << op2 << "());\n";
		}
		break;
	default:
		assert(false);
		break;
	}
}

void Builder_Cpp::emit_struct2(Ast_Struct_Decl* s, std::ostream& os, bool is_exception) {
	auto make_struct = [s, this, &os]<typename T>(T && fn) {
		os << "struct " << s->name << " {\n";
		for (auto const f : s->fields) {
			os << "  ";
			fn(f->type, os);
			os << ' ' << f->name << ";\n";
		}
		os << "};\n\n";
	};

	if (!is_exception) {
		make_struct(std::bind(&Builder_Cpp::emit_type, this, _1, _2));
	} else {
		os << "class " << s->name << " : public ::nprpc::Exception {\n"
			"public:\n"
			;

		if (s->fields.size() > 1) {
			std::for_each(next(begin(s->fields)), end(s->fields),
				[this, &os](auto f) {
					os << "  "; emit_type(f->type, os); os << " " << f->name << ";\n";
				});
			os << '\n';
		}

		os <<
			"  " << s->name << "() : ::nprpc::Exception(\"" << s->name << "\") {} \n"
			;

		if (s->fields.size() > 1) {
			os << "  " << s->name << '('; std::for_each(next(begin(s->fields)), end(s->fields), 
				[this, &os, ix = 1, size = s->fields.size()](auto f) mutable {
				emit_type(f->type, os); os << " _" << f->name;
				if (++ix < size) os << ", ";
			});
		

		os << ")\n"
			"    : ::nprpc::Exception(\"" << s->name << "\")\n"
			;

			std::for_each(next(begin(s->fields)),
				end(s->fields), [this, &os](auto f) mutable {
					os << "    , " << f->name << "(_" << f->name << ")\n";
				});
		
			os << 
			"  {\n"
			"  }\n"
			;
		}
		
		

		os << "};\n\n";
	}
	
	os << "namespace flat {\n";
	make_struct(std::bind(&Builder_Cpp::emit_flat_type, this, _1, _2));

	auto const accessor_name = s->name + "_Direct";

	os << "class " << accessor_name << " {\n"
		"  boost::beast::flat_buffer& buffer_;\n"
		"  const size_t offset_;\n\n"
		"  auto& base() noexcept { return *reinterpret_cast<" << s->name <<
		"*>(reinterpret_cast<std::byte*>(buffer_.data().data()) + offset_); }\n"
		"  auto const& base() const noexcept { return *reinterpret_cast<const " << s->name <<
		"*>(reinterpret_cast<const std::byte*>(buffer_.data().data()) + offset_); }\n"
		"public:\n"
		"  void* __data() noexcept { return (void*)&base(); }\n"
		"  " << accessor_name << "(boost::beast::flat_buffer& buffer, size_t offset)\n"
		"    : buffer_(buffer)\n"
		"    , offset_(offset)\n"
		"  {\n"
		"  }\n"
		;

	for (auto& f : s->fields) {
		emit_accessors(s->name, f, os);
	}

	os << "};\n";
	os << "} // namespace flat\n\n";
}

void Builder_Cpp::emit_struct(Ast_Struct_Decl* s) {
	emit_struct2(s, oh, false);
}

void Builder_Cpp::emit_exception(Ast_Struct_Decl* s) {
	assert(s->is_exception());
	emit_struct2(s, oh, true);
}

void Builder_Cpp::emit_file_footer() {
	auto& exs = ctx_.exceptions;

	if (!exs.empty()) {

		oc <<
			"\n"
			"void " << ctx_.base_name << "_throw_exception(boost::beast::flat_buffer& buf) { \n"
			"  switch(*(uint32_t*)( (char*)buf.data().data() + sizeof(::nprpc::impl::Header)) ) {\n"
			;

		always_full_namespace(true);
		for (auto ex : exs) {
			oc <<
				"  case " << ex->exception_id << ":\n"
				"  {\n"
				"    " << ns(ex->nm) << "flat::" <<ex->name << "_Direct ex_flat(buf, sizeof(::nprpc::impl::Header));\n"
				"    " << ns(ex->nm) << ex->name << " ex;\n"
				;

			for (size_t i = 1; i < ex->fields.size(); ++i) {
				auto f = ex->fields[i];
				assign_from_flat_type(f->type, "ex." + f->name, "ex_flat." + f->name, false, true);
			}


			oc <<
				"    throw ex;\n"
				"  }\n"
				;
		}
		always_full_namespace(false);


		oc <<
			"  default:\n"
			"    throw std::runtime_error(\"unknown rpc exception\");\n"
			"  }\n"
			"}\n"
			;
	}

	emit_arguments_structs(std::bind(&Builder_Cpp::emit_struct2, this, _1, std::ref(ohm), false));
	emit_helpers();
	
	oh << "\n#endif";
	ohm << "\n#endif";
}

void Builder_Cpp::emit_namespace_begin() {
	oh << "namespace " << ctx_.nm_cur()->name() << " { \n";
	oc << "namespace " << ctx_.nm_cur()->name() << " { \n";
}

void Builder_Cpp::emit_namespace_end() {
	oh << "} // namespace " << ctx_.nm_cur()->name() << "\n\n";
	oc << "} // namespace " << ctx_.nm_cur()->name() << "\n\n";
}

void Builder_Cpp::emit_helpers() {
	oh << "namespace " << ctx_.base_name << "::helper {\n";
	for (auto& [unused, s] : ctx_.affa_list) {
		for (auto f : s->fields) {
			assert(f->function_argument);
			if (f->input_function_argument) {
				continue;
			} else {
				if (is_fundamental(f->type) || f->type->id == FieldType::String || f->type->id == FieldType::Object) continue;
				oh << 
					"template<::nprpc::IterableCollection T>\n"
					"void assign_" << f->function_name << "_" << f->function_argument_name << "("; emit_parameter_type_for_servant_callback_r(f->type, oh, false); oh << "& dest, const T & src) {\n"
					;
				assign_from_cpp_type(f->type, "dest", "src", oh, false, true);
			}
			oh << "}\n";
		}
	}

	oh << "} // namespace " << ctx_.base_name << "::helper\n";
}

void Builder_Cpp::emit_interface(Ast_Interface_Decl* ifs) {
	auto emit_function_arguments = [](Ast_Function_Decl* fn, std::ostream& os,
		std::function<void(Ast_Function_Argument*, std::ostream& os)> emitter) {
			os << "(";
			size_t ix = 0;
			for (auto arg : fn->args) {
				emitter(arg, os);
				os << " " << arg->name;
				if (++ix != fn->args.size()) os << ", ";
			}
			os << ')';
	};

	// Servant definition
	oh <<
		"class I" << ifs->name << "_Servant\n";
	if (ifs->plist.size()) {
		oh << "  : public I" << ifs->plist[0]->name << "_Servant\n";
		for (size_t i = 1; i < ifs->plist.size(); ++i) {
			oh << "  , public I" << ifs->plist[i]->name << "_Servant\n";
		}
		oh << "{\n";
	} else {
		oh << "  : public virtual nprpc::ObjectServant\n{\n";
	}

	oh <<
		"public:\n"
		"  static std::string_view _get_class() noexcept { return \"" << ctx_.base_name << '/' << ctx_.nm_cur()->to_interface_path() << '.' << ifs->name << "\"; }\n"
		"  std::string_view get_class() const noexcept override { return I" << ifs->name << "_Servant::_get_class(); }\n"
		"  void dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) override;\n"
		;

	for (auto fn : ifs->fns) {
		oh << "  virtual "; emit_type(fn->ret_value, oh);
		oh << ' ' << fn->name << " ";
		emit_function_arguments(fn, oh,
			std::bind(&Builder_Cpp::emit_parameter_type_for_servant_callback, this, _1, _2)
		);
		oh << " = 0;\n";
	}

	oh << "};\n\n";

	// Proxy definition
	oh <<
		"class " << ifs->name << "\n";

	if (ifs->plist.size()) {
		oh << "  : public " << ifs->plist[0]->name << "\n";
		for (size_t i = 1; i < ifs->plist.size(); ++i) {
			oh << "  , public " << ifs->plist[i]->name << "\n";
		}
		oh << "{\n";
	} else {
		oh << "  : public virtual nprpc::Object\n{\n";
	}

	oh <<
		"  const uint8_t interface_idx_;\n"
		"public:\n"
		"  using servant_t = I" << ifs->name << "_Servant;\n\n"
		;

	if (ifs->plist.empty()) {
		oh << "  " << ifs->name << "(uint8_t interface_idx) : interface_idx_(interface_idx) {}\n";
	} else {
		oh <<
			"  " << ifs->name << "(uint8_t interface_idx)\n"
			"    : interface_idx_(interface_idx)\n"
			;

		auto count_all = [](Ast_Interface_Decl* ifs_inherited, int& n) { ++n; };

		int n = 1;
		for (auto parent : ifs->plist) {
			oh <<
				"    , " << parent->name << "(interface_idx + " << n << ")\n"
				;
			dfs_interface(std::bind(count_all, _1, std::ref(n)), parent);
		}
		oh <<
			"  {\n"
			"  }\n"
			;
	}


	// functions definitions
	for (auto& fn : ifs->fns) {
		oh << "  "; emit_type(fn->ret_value, oh);
		oh << ' ' << fn->name << " ";
		emit_function_arguments(fn, oh,
			std::bind(&Builder_Cpp::emit_parameter_type_for_proxy_call, this, _1, _2)
		);
		oh << ";\n";
	}

	oh << "};\n\n";


	// .CPP file marshall/unmarshall stuff below
	auto const nm = ctx_.nm_cur()->to_cpp17_namespace();

	for (auto fn : ifs->fns) {
		make_arguments_structs(fn);

		emit_type(fn->ret_value, oc);
		oc << ' ' << nm << "::" << ifs->name << "::" << fn->name;
		emit_function_arguments(fn, oc,
			std::bind(&Builder_Cpp::emit_parameter_type_for_proxy_call, this, _1, _2)
		);
		oc << " {\n";


		oc << "  boost::beast::flat_buffer buf;\n";

		const auto fixed_size = get_arguments_offset() + (fn->in_s ? fn->in_s->size : 0);
		const auto capacity = fixed_size + (fn->in_s ? (fn->in_s->flat ? 0 : 128) : 0);

		oc <<
			"  {\n"
			"    auto mb = buf.prepare(" << capacity << ");\n"
			"    buf.commit(" << fixed_size << ");\n"
			"    static_cast<::nprpc::impl::Header*>(mb.data())->msg_id = ::nprpc::impl::MessageId::FunctionCall;\n"
			"    static_cast<::nprpc::impl::Header*>(mb.data())->msg_type = ::nprpc::impl::MessageType::Request;\n"
			"  }\n"
			"  ::nprpc::impl::flat::CallHeader_Direct __ch(buf, sizeof(::nprpc::impl::Header));\n"
			"  __ch.object_id() = this->_data().object_id;\n"
			"  __ch.poa_idx() = this->_data().poa_idx;\n"
			"  __ch.interface_idx() = interface_idx_;\n"
			"  __ch.function_idx() = " << fn->idx << ";\n"
			;

		if (fn->in_s) {
			oc <<
				"  ::flat::" << fn->in_s->name << "_Direct _(buf," << get_arguments_offset() << ");\n"
				;
		}

		int ix = 0;
		for (auto in : fn->args) {
			if (in->modifier == ArgumentModifier::Out) continue;
			assign_from_cpp_type(in->type, "_._" + std::to_string(++ix), in->name, oc);
		}

		oc << "  static_cast<::nprpc::impl::Header*>(buf.data().data())->size = static_cast<uint32_t>(buf.size() - 4);\n";

		oc <<
			"  ::nprpc::impl::g_orb->call(\n"
			"    nprpc::EndPoint(this->_data().ip4, this->_data().port), buf, this->get_timeout()\n"
			"  );\n"
			"  auto std_reply = nprpc::impl::handle_standart_reply(buf);\n"
			;

		if (fn->ex) {
			oc <<
				"  if (std_reply == 1) {\n"
				"    " << ctx_.base_name << "_throw_exception(buf);\n"
				"  }\n"
				;
		}

		if (!fn->out_s) {
			oc <<
				"  if (std_reply != 0) {\n"
				"    std::cerr << \"received an unusual reply for function with no output arguments\\n\";\n"
				//"    assert(false);\n"
				"  }\n"
				;
		} else {
			oc <<
				"  if (std_reply != -1) {\n"
				"    std::cerr << \"received an unusual reply for function with output arguments\\n\";\n"
				//"    assert(false);\n"
				"    throw nprpc::Exception(\"Unknown Error\");\n"
				"  }\n"
				;

			oc << "  ::flat::" << fn->out_s->name << "_Direct out(buf, sizeof(::nprpc::impl::Header));\n";

			int ix = fn->is_void() ? 0 : 1;

			for (auto out : fn->args) {
				if (out->modifier == ArgumentModifier::In) continue;
				assign_from_flat_type(out->type, out->name, "out._" + std::to_string(++ix), false, true);
			}

			if (!fn->is_void()) {
				oc << "  "; emit_type(fn->ret_value, oc); oc << " __ret_value;\n";
				assign_from_flat_type(fn->ret_value, "__ret_value", "out._1", false, true);
				oc << "  return __ret_value;\n";
			}
		}

		oc << "}\n\n";
	}

	// Servant dispatch
	oc << "void " << nm << "::I" << ifs->name << "_Servant::dispatch(nprpc::Buffers& bufs, nprpc::EndPoint remote_endpoint, bool from_parent, nprpc::ReferenceList& ref_list) {\n"
		"  nprpc::impl::flat::CallHeader_Direct __ch(bufs(), sizeof(::nprpc::impl::Header));\n"
		;
		
	if (ifs->plist.empty()) {
		// ok
	} else {
		oc <<
			"  if (from_parent == false) {\n"
			"    switch(__ch.interface_idx()) {\n"
			"      case 0:\n"
			"        break;\n"
			;
	
		int ix = 1;
		auto select_interface = [&ix, this, ifs](Ast_Interface_Decl* i) {
			if (i == ifs) return;
			oc <<
				"      case "<< ix << ":\n"
				"        I" << i->name << "_Servant::dispatch(bufs, remote_endpoint, true, ref_list);\n"
				"        return;\n"
			;
			++ix;
		};
		
		dfs_interface(select_interface, ifs);
		
		oc <<
			"      default:\n"
			//"        assert(false);\n"
			"        throw \"unknown interface\";\n"
			"    }\n"
			"  }\n"
			;
	}

	oc <<
			"  switch(__ch.function_idx()) {\n"
			;


	for (auto fn : ifs->fns) {
		oc << "    case " << fn->idx << ": {\n";

		int out_ix = fn->is_void() ? 0 : 1;

		if (fn->out_s && fn->out_s->flat) {
			for (auto arg : fn->args) {
				if (arg->modifier == ArgumentModifier::Out) {
					oc << "      "; emit_type(arg->type, oc); oc << " _out_" << ++out_ix << ";\n";
				}
			}
		}

		if (fn->in_s) {
			oc <<
				"      ::flat::" << fn->in_s->name << "_Direct ia(bufs(), " << get_arguments_offset() << ");\n"
				;
		}

		if (fn->out_s && !fn->out_s->flat) {
			const auto offset = size_of_header;
			const auto initial_size = offset + fn->out_s->size;
			oc <<
				"      auto& obuf = bufs.flip();\n"
				"      obuf.consume(obuf.size());\n"
				"      obuf.prepare(" << initial_size + 128 << ");\n"
				"      obuf.commit(" << initial_size << ");\n"
				"      ::flat::" << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
				;
		}

		if (!fn->is_void()) {
			emit_type(fn->ret_value, oc); oc << " __ret_val;\n";
		}

		if (fn->ex) oc << "      try {\n";
		
		oc <<
			(fn->is_void() ? "      " : "      __ret_val = ") << fn->name << "("
			;

		int in_ix = 0, idx = 0; out_ix = fn->is_void() ? 0 : 1;
		for (auto arg : fn->args) {
			if (arg->modifier == ArgumentModifier::Out) {
				assert(fn->out_s);
				if (!fn->out_s->flat) {
					oc << "oa._" << ++out_ix;
					if (arg->type->id == FieldType::Vector || arg->type->id == FieldType::String) {
						oc << "_vd()";
					} else {
						oc << "()";
					}
				} else {
					oc << "_out_" << ++out_ix;
				}
			} else {
				if (arg->type->id == FieldType::Object) {
					oc << "nprpc::impl::g_orb->create_object_from_flat(ia._" << ++in_ix << "(), remote_endpoint)";
				} else {
					oc << "ia._" << ++in_ix << "()";
				}
			}
			if (++idx != fn->args.size()) oc << ", ";
		}
		oc << ");\n";

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
			oc <<
				"      }\n"
				"      catch(" << ns(fn->ex->nm) << fn->ex->name << "& e) {\n"
				"        auto& obuf = bufs();\n"
				"        obuf.consume(obuf.size());\n"
				"        obuf.prepare(" << initial_size << ");\n"
				"        obuf.commit(" << initial_size << ");\n"
				"        "<< ns(fn->ex->nm) << "flat::" <<fn->ex->name << "_Direct oa(obuf," << offset << ");\n"
				"        oa.__ex_id() = " << fn->ex->exception_id << ";\n"
				;
			always_full_namespace(false);

			for (auto i = 1; i < fn->ex->fields.size(); ++i) {
				auto mb = fn->ex->fields[i];
				assign_from_cpp_type(mb->type, "oa." + mb->name, "e." + mb->name, oc);
			}

			oc << 
				"        static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);\n"
				"        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::Exception;\n"
				"        static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;\n"
				"        return;\n"
				"      }\n"
				;
		}

		if (!fn->out_s) {
			oc << "      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Success);\n";
		} else {
			if (fn->out_s->flat) { // it means that we are writing output data in the input buffer, 
				// so we must pass stack variables first and then assign result back to the buffer
				const auto offset = size_of_header;
				const auto initial_size = offset + fn->out_s->size;

				oc <<
					"      auto& obuf = bufs();\n"
					"      obuf.consume(obuf.size());\n"
					"      obuf.prepare(" << initial_size << ");\n"
					"      obuf.commit(" << initial_size << ");\n"
					"      ::flat::" << fn->out_s->name << "_Direct oa(obuf," << offset << ");\n"
					;
				
				int ix;
				if (!fn->is_void()) {
					assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
					ix = 1;
				} else {
					ix = 0;
				}

				for (auto out : fn->args) {
					if (out->modifier == ArgumentModifier::In) continue;
					auto n = std::to_string(++ix);
					assign_from_cpp_type(out->type, "oa._" + n, "_out_" + n, oc);
				}
			} else if (!fn->is_void()) {
				assign_from_cpp_type(fn->ret_value, "oa._1", "__ret_val", oc);
			}

			oc <<
				"      static_cast<::nprpc::impl::Header*>(obuf.data().data())->size = static_cast<uint32_t>(obuf.size() - 4);\n"
				"      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_id = ::nprpc::impl::MessageId::BlockResponse;\n"
				"      static_cast<::nprpc::impl::Header*>(obuf.data().data())->msg_type = ::nprpc::impl::MessageType::Answer;\n"
				;
		}

		oc <<
			"      break;\n"
			"    }\n"
			;
	}

	oc <<
		"    default:\n"
		"      nprpc::impl::make_simple_answer(bufs(), nprpc::impl::MessageId::Error_UnknownFunctionIdx);\n"
		"  }\n"; // switch block
	;

	oc << "}\n\n"; // dispatch
}

void Builder_Cpp::emit_using(Ast_Alias_Decl* u) {
	oh << "using " << u->name << " = "; emit_type(u->type, oh); oh << ";\n";
}

void Builder_Cpp::emit_enum(Ast_Enum_Decl* e) {
	oh << "enum class " << e->name << " : " << fundamental_to_cpp(e->token_id) << " {\n";
	int64_t ix = 0;
	for (size_t i = 0; i < e->items.size(); ++i) {
		oh << "  " << e->items[i].first;
		auto const n = e->items[i].second;
		if (n.second || ix != n.second) { // explicit
			oh << " = " << n.first;
			ix = n.first.decimal() + 1;
		} else {
			++ix;
		}
		if (i != e->items.size() - 1) oh << ",\n";
	}
	oh << "\n};\n";
}

Builder_Cpp::Builder_Cpp(Context& ctx, std::filesystem::path file_path, 
	std::filesystem::path out_inc_path, std::filesystem::path out_src_path)
	: Builder(ctx)
	, file_path_(file_path) {

	auto filename = file_path.filename();

	auto const header_file_path = filename.replace_extension(".hpp");
	auto const cpp_file_path = filename.replace_extension(".cpp");
	auto const m_file_path = filename.replace_extension() += ("_m.hpp");
	
	oh.open(out_inc_path / header_file_path);
	ohm.open(out_inc_path / m_file_path);
	oc.open(out_src_path / cpp_file_path);

	if (!oh || !ohm || !oc) {
		throw std::runtime_error("Could not create output file...");
	}

	auto make_guard = [](const std::string& file) {
		std::string r(file);
		std::transform(r.begin(), r.end(), r.begin(), [](char c) {
			return c == '.' ? '_' : ::toupper(c);
			});
		return r + '_';
	};


	auto h1 = make_guard(ctx_.base_name);
	auto h2 = make_guard(ctx_.base_name + "_M");

	oh <<
		"#ifndef " << h1 << "\n"
		"#define " << h1 << "\n\n"
		"#include <nprpc/flat.hpp>\n";

	if (!ctx_.is_nprpc_base()) {
		oh << "#include <nprpc/nprpc.hpp>\n\n";
	} else {
		oh << "#include <nprpc/exception.hpp>\n\n";
	}

	ohm <<
		"#ifndef " << h2 << "\n"
		"#define " << h2 << "\n\n";

	oc <<
		"#include \"" << ctx_.base_name << ".hpp\"\n"
		"#include \"" << ctx_.base_name << "_m.hpp\"\n"
		"#include <nprpc/nprpc_impl.hpp>\n\n"
		"void " << ctx_.base_name << "_throw_exception(boost::beast::flat_buffer& buf);\n\n"
		;
}

/*
void Builder_Cpp::emit_msg_class(Ast_MessageDescriptor_Decl* m, std::ostream& os) {
	auto s = m->s;
	auto const& msg_name = m->name;

	os << "class " << msg_name << " {\n";

	os << "  boost::beast::flat_buffer buffer_;\n";
	os << "  " << s->name + "_Direct acc_;\n\n";

	os << "public:\n";

	os << "  " << s->name << "_Direct* operator->() noexcept { return &acc_; }\n";
	os << "  const " << s->name << "_Direct* operator->() const noexcept { return &acc_; }\n";
	os << "  auto&& detach() noexcept { return std::move(buffer_); }\n";


	os << '\n';
	os << "  " << msg_name << "()\n    : acc_(buffer_, 8)\n  {\n";
	os << "    constexpr size_t initial_size = " << "sizeof(" << s->name << ") + 8;\n";
	os << "    buffer_.prepare(initial_size);\n";
	os << "    buffer_.commit(initial_size);\n";
	os << "    *(MessageId*)buffer_.data().data() = (MessageId)" << m->message_id << ";\n";
	os << "  }\n";
	os << "  " << msg_name << "(boost::beast::flat_buffer&& buffer)\n"
		"    : buffer_(std::move(buffer))\n    , acc_(buffer_, 8)\n  {\n  }\n";
	os << "  " << msg_name << "(" << msg_name << "&& other)\n"
		"    : buffer_(std::move(other.buffer_))\n    , acc_(buffer_, 8)\n  {\n  }\n";

	os << "};\n\n";
}

void Builder_Cpp::emit_message_map(std::vector<Ast_MessageDescriptor_Decl*>& lst) {
	oh << "enum class MessageId : std::uint32_t {\n";
	for (auto msg : lst) oh << "  " << msg->name << " = " << msg->message_id << ",\n";
	oh << "};\n\n";
}
*/