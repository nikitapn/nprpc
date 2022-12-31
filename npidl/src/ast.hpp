// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cassert>
#include <filesystem>
#include <variant>

constexpr int fundamental_type_first = 256;
constexpr int fundamental_type_last = fundamental_type_first + 16;

#define ONE_CHAR_TOKENS() \
	TOKEN_FUNC(Hash, '#') \
	TOKEN_FUNC(RoundBracketOpen, '(') \
	TOKEN_FUNC(RoundBracketClose, ')') \
	TOKEN_FUNC(Comma, ',') \
	TOKEN_FUNC(Semicolon, ';') \
	TOKEN_FUNC(Assignment, '=') \
	TOKEN_FUNC(Optional, '?') \
	TOKEN_FUNC(Less, '<') \
	TOKEN_FUNC(Greater, '>') \
	TOKEN_FUNC(SquareBracketOpen, '[') \
	TOKEN_FUNC(SquareBracketClose, ']') \
	TOKEN_FUNC(BracketOpen, '{') \
	TOKEN_FUNC(BracketClose, '}')

enum class TokenId {
	Colon = ':',

#define TOKEN_FUNC(x, y) x = y,
	ONE_CHAR_TOKENS()
#undef TOKEN_FUNC

	Boolean = fundamental_type_first,
	Int8,
	UInt8,
	Int16,
	UInt16,
	Int32,
	UInt32,
	Int64,
	UInt64,
	Float32,
	Float64 = fundamental_type_last,

	Name,
	Number,
	String,
	Vector,
	Flat,
	Exception,
	Eof,
	DoubleColon,
	Namespace,
	Interface,
	Object,
	Void,
	In,
	Out,
	Enum,
	Using,
	Raises,
	OutDirect,
	Helper,
	Trusted,
	Async,
};

struct Ast_Struct_Decl;
struct Ast_Type_Decl;
struct Ast_Interface_Decl;
struct Ast_Function_Decl;

class Namespace {
	Namespace* parent_;
	std::string name_;

	std::vector<Namespace*> children_;
	std::vector<std::pair<std::string, Ast_Type_Decl*>> types_;

	std::string construct_path(const std::string& delim, bool omit_root = false) const noexcept;
public:
	Namespace();
	Namespace(Namespace* parent, std::string&& name);
	auto push(std::string&& s) noexcept;
	Ast_Type_Decl* find_type(const std::string& str, bool only_this_namespace);
	Namespace* find_child(const std::string& str);
	void add(const std::string& name, Ast_Type_Decl* type);
	const std::string& name() const noexcept;
	Namespace* parent() const noexcept;
	std::string to_cpp17_namespace() const noexcept;
	std::string to_ts_namespace() const noexcept;
	std::string to_interface_path() const noexcept;
};

enum class FieldType {
	Fundamental,
	Array,
	Vector,
	String,
	Struct,
	Optional,
	Void,
	Object,
	Interface,
	Alias,
	Enum,
};

enum class NumberFormat { Decimal, Hex, Scientific };

struct Ast_Number {
	std::variant<std::int64_t, float, double, bool> value;
	NumberFormat format;
	int max_size = 0;

	std::int64_t decimal() const noexcept {
		assert(std::holds_alternative<std::int64_t>(value));
		return std::get<std::int64_t>(value);
	}
};

inline bool operator == (std::int64_t x, const Ast_Number& n) {
	assert(std::holds_alternative<std::int64_t>(n.value));
	return std::get<std::int64_t>(n.value) == x;
}

inline bool operator != (std::int64_t x, const Ast_Number& n) {
	return !::operator==(x, n);
}

// helper type for the visitor #4
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
// explicit deduction guide (not needed as of C++20)
//template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

inline std::ostream& operator << (std::ostream& os, const Ast_Number& n) {
	std::visit(overloaded{
		[&](int64_t x) { 
			if (n.format == NumberFormat::Hex) {
				os << "0x" << std::hex;
				if (n.max_size) {
					os << std::setfill('0') << std::setw(static_cast<std::streamsize>(n.max_size) * 2);
				}
				os << x;
				os << std::dec;
			} else {
				os << x;
			}
		},
		[&](float x) { os << x; },
		[&](double x) { os << x; },
		[&](bool x) { os << x; },
		}, n.value);
	return os;
}


struct Ast_Type_Decl {
	FieldType id;
};

struct Ast_Object_Decl : Ast_Type_Decl {
	Ast_Object_Decl() {
		id = FieldType::Object;
	}
};

struct Ast_Void_Decl : Ast_Type_Decl {
	Ast_Void_Decl() {
		id = FieldType::Void;
	}
};

struct Ast_Fundamental_Type : Ast_Type_Decl {
	TokenId token_id;

	Ast_Fundamental_Type(TokenId _token_id)
		: token_id(_token_id) 
	{
		id = FieldType::Fundamental;
	}
};

struct Ast_Wrap_Type : Ast_Type_Decl {
	Ast_Type_Decl* type;
	Ast_Wrap_Type(Ast_Type_Decl* _type) : type(_type) {}

	Ast_Type_Decl* real_type();
};

struct Ast_Array_Decl : Ast_Wrap_Type {
	const int length;

	Ast_Array_Decl(Ast_Type_Decl* _type, int _length) 
		: Ast_Wrap_Type(_type)
		, length(_length) 
	{
		id = FieldType::Array;
	}
};

struct Ast_Vector_Decl : Ast_Wrap_Type {
	Ast_Vector_Decl()
		: Ast_Wrap_Type(nullptr) 
	{
		id = FieldType::Vector;
	}
};

struct Ast_Alias_Decl : Ast_Wrap_Type {
	Namespace* nm;
	std::string name;

	Ast_Type_Decl* get_real_type() const noexcept {
		auto t = this;
		while (t->type->id == FieldType::Alias) t = static_cast<Ast_Alias_Decl*>(t->type);
		return t->type;
	}

	Ast_Alias_Decl(std::string&& _name,Namespace* _nm, Ast_Type_Decl* _type)
		: Ast_Wrap_Type(_type)
		, name(_name)
		,	nm(_nm)
	{
		id = FieldType::Alias;
	}
};

struct Ast_String_Decl : Ast_Type_Decl {
	Ast_String_Decl() {
		id = FieldType::String;
	}
};

struct Ast_Interface_Decl : Ast_Type_Decl {
	std::string name;
	std::vector<Ast_Function_Decl*> fns;
	std::vector<Ast_Interface_Decl*> plist;
	bool trusted = true;


	Ast_Interface_Decl() {
		id = FieldType::Interface;
	}
};

struct Ast_Enum_Decl : Ast_Fundamental_Type {
	std::string name;
	Namespace* nm;
	std::vector<std::pair<std::string, std::pair<Ast_Number, bool>>> items;
	
	Ast_Enum_Decl() 
		: Ast_Fundamental_Type(TokenId::UInt32) 
	{
		id = FieldType::Enum;
	}
};

struct Ast_Field_Decl {
	Ast_Type_Decl* type;
	std::string name;
	bool function_argument = false;
	bool input_function_argument;
	std::string_view function_name;
	std::string_view function_argument_name;
	
	bool is_optional() const noexcept {
		return type->id == FieldType::Optional;
	}
};

using struct_id_t = std::string;

struct Ast_Struct_Decl : Ast_Type_Decl {
	int version = -1;
	Namespace* nm;
	std::string name;
	struct_id_t unique_id;
	std::vector<Ast_Field_Decl*> fields;
	int size = -1;
	int align = -1;
	bool flat = true;
	bool has_span_class = false;
	uint32_t exception_id;

	bool is_exception() const noexcept { return exception_id != -1; }
	const struct_id_t& get_function_struct_id();

	Ast_Struct_Decl() {
		id = FieldType::Struct;
	}
};

struct Ast_Optional_Decl : Ast_Wrap_Type {
	Ast_Optional_Decl(Ast_Type_Decl* _type)
		: Ast_Wrap_Type(_type)
	{
		id = FieldType::Optional;
	}
};

constexpr auto cft(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Fundamental || type->id == FieldType::Enum);
	return static_cast<Ast_Fundamental_Type*>(type);
}

constexpr auto cft(const Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Fundamental || type->id == FieldType::Enum);
	return static_cast<const Ast_Fundamental_Type*>(type);
}

constexpr auto cwt(Ast_Type_Decl* type) noexcept {
	assert(
		type->id == FieldType::Array ||
		type->id == FieldType::Vector ||
		type->id == FieldType::Alias ||
		type->id == FieldType::Optional
	);
	return static_cast<Ast_Wrap_Type*>(type);
}

constexpr auto car(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Array);
	return static_cast<Ast_Array_Decl*>(type);
}

constexpr auto cvec(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Vector);
	return static_cast<Ast_Vector_Decl*>(type);
}

constexpr auto cstr(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::String);
	return static_cast<Ast_String_Decl*>(type);
}

constexpr auto cobj(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Object);
	return static_cast<Ast_Object_Decl*>(type);
}

constexpr auto cflat(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Struct);
	return static_cast<Ast_Struct_Decl*>(type);
}

constexpr auto cenum(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Enum);
	return static_cast<Ast_Enum_Decl*>(type);
}

constexpr auto cenum(const Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Enum);
	return static_cast<const Ast_Enum_Decl*>(type);
}

constexpr auto calias(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Alias);
	return static_cast<Ast_Alias_Decl*>(type);
}

constexpr auto calias(const Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Alias);
	return static_cast<const Ast_Alias_Decl*>(type);
}

constexpr auto cifs(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Interface);
	return static_cast<Ast_Interface_Decl*>(type);
}

constexpr auto copt(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Optional);
	return static_cast<Ast_Optional_Decl*>(type);
}


inline Ast_Type_Decl* Ast_Wrap_Type::real_type() {
	auto wt = type;
	if (wt->id == FieldType::Alias) wt = calias(wt)->get_real_type();
	return wt;
}

enum class ArgumentModifier { In, Out };

struct Ast_Function_Argument : Ast_Field_Decl {
	ArgumentModifier modifier;
	bool direct = false;
};

struct Ast_Function_Decl {
	uint16_t idx;
	std::string name;
	Ast_Type_Decl* ret_value;
	Ast_Struct_Decl* in_s = nullptr;
	Ast_Struct_Decl* out_s = nullptr;
	bool arguments_structs_have_been_made = false;
	Ast_Struct_Decl* ex = nullptr;
	std::vector<Ast_Function_Argument*> args, in_args, out_args;
	bool is_async;

	bool is_void() const noexcept { return ret_value->id == FieldType::Void; }
};


template<typename IdType, typename T>
class List {
	static_assert(std::is_pointer_v<T>);
	std::vector<std::tuple<IdType, T>> items_;
public:
	T get(const IdType& id) {
		if (auto it = std::find_if(items_.begin(), items_.end(),
			[id](auto& t) { return std::get<0>(t) == id; }); it != items_.end()) {
			return std::get<1>(*it);
		}
		return nullptr;
	}
	void put(const IdType& id, T item) {
		items_.emplace_back(id, item);
	}
	auto begin() { return items_.begin(); }
	auto end() { return items_.end(); }
};


using AFFAList = List<struct_id_t, Ast_Struct_Decl*>;


inline const std::string& Namespace::name() const noexcept { return name_; }
inline Namespace* Namespace::parent() const noexcept { return parent_; }

inline auto Namespace::push(std::string&& s) noexcept {
	children_.push_back(new Namespace(this, std::move(s)));
	return children_.back();
}

inline Ast_Type_Decl* Namespace::find_type(const std::string& str, bool only_this_namespace) {
	if (auto it = std::find_if(types_.begin(), types_.end(),
		[&str](auto const& pair) { return pair.first == str; }); it != types_.end()) {
		return it->second;
	}

	if (only_this_namespace == false && parent()) return parent()->find_type(str, false);

	return nullptr; // throw_error("Unknown type: \"" + str + "\"");
}

inline Namespace* Namespace::find_child(const std::string& str) {
	if (auto it = std::find_if(children_.begin(), children_.end(),
		[&str](auto nm) {return nm->name() == str; }); it != children_.end()) {
		return *it;
	}
	return nullptr;
}

inline void Namespace::add(const std::string& name, Ast_Type_Decl* type) {
	if (std::find_if(std::begin(types_), std::end(types_), [&name](const auto& pair) { return pair.first == name; }) != std::end(types_)) {
		throw "type redefinition";
	}
	types_.push_back({ name, type });
}

inline std::string Namespace::construct_path(const std::string& delim, bool omit_root) const noexcept {
	if (omit_root && parent() && parent()->name().empty()) return {};
	
	auto ptr = parent();
	std::string result = name();
	while (ptr && !ptr->name().empty()) {
		if (omit_root && ptr->parent() && ptr->parent()->name().empty()) break;
		result = ptr->name() + delim + result;
		ptr = ptr->parent();
	}
	return result;
}

inline std::string Namespace::to_cpp17_namespace() const noexcept {
	static const auto delim = std::string(2, ':');
	return construct_path(delim);
}

inline std::string Namespace::to_ts_namespace() const noexcept {
	static const auto delim = std::string(2, '.');
	return construct_path(delim, true);
}

inline std::string Namespace::to_interface_path() const noexcept {
	static const auto delim = std::string(1, '.');
	return construct_path(delim);
}

inline Namespace::Namespace()
	: parent_(nullptr)
{
}

inline Namespace::Namespace(Namespace* parent, std::string&& name)
	: parent_(parent)
	, name_(std::move(name))
{
}

class Context {
	Namespace* nm_root_;
	Namespace* nm_cur_;
	uint32_t exception_id_last = -1;
public:
	std::string base_name;
	AFFAList affa_list;
	int m_struct_n_ = 0;
	std::vector<Ast_Struct_Decl*> exceptions;
	std::vector<Ast_Interface_Decl*> interfaces;

	uint32_t next_exception_id () noexcept { return ++exception_id_last; }

	auto push_namespace(std::string&& s) {
		nm_cur_ = nm_cur_->push(std::move(s));
		return nm_cur_;
	}

	void pop_namespace() {
		nm_cur_ = nm_cur_->parent();
		assert(nm_cur_);
	}

	Namespace* nm_cur() { return nm_cur_; }
	Namespace* nm_root() { return nm_root_; }

	bool is_nprpc_base() const noexcept {
		return base_name == "nprpc_base";
	}

	bool is_nprpc_nameserver() const noexcept {
		return base_name == "nprpc_nameserver";
	}

	Context(const std::filesystem::path& file_path) {
		base_name = file_path.filename().replace_extension().string();
		std::transform(base_name.begin(), base_name.end(), base_name.begin(), [](char c) {
			return c == '.' ? '_' : ::tolower(c); });
		nm_root_ = nm_cur_ = new Namespace();
	}
};