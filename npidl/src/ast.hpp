// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cassert>

constexpr int fundamental_type_first = 256;
constexpr int fundamental_type_last = fundamental_type_first + 16;

enum class TokenId {
	Hash = '#',
	RoundBracketOpen = '(',
	RoundBracketClose = ')',
	Comma = ',',
	Colon = ':',
	Semicolon = ';',
	Less = '<',
	Assignment = '=',
	Greater = '>',
	SquareBracketOpen = '[',
	SquareBracketClose = ']',
	BracketOpen = '{',
	BracketClose = '}',

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
	Message,
	Of,
	Namespace,
	Interface,
	Object,
	Void,
	In,
	Out,
	Enum,
	Using,
	Raises
};

struct Ast_Struct_Decl;
struct Ast_Type_Decl;
struct Ast_Interface_Decl;
struct Ast_Function_Decl;

class Namespace {
	std::vector<Namespace*> childs_;
	Namespace* parent_;
	std::string name_;
	std::vector<std::pair<std::string, Ast_Type_Decl*>> types_;
	std::string construct_path(const std::string& delim) const noexcept;
	
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
	std::string to_interface_path() const noexcept;
};

enum class FieldType {
	Fundamental,
	Array,
	Vector,
	String,
	Struct,
	Void,
	Object,
	Interface,
	Alias,
	Enum,
	Message,
};

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

	Ast_Interface_Decl() {
		id = FieldType::Interface;
	}
};

struct Ast_Enum_Decl : Ast_Fundamental_Type {
	std::string name;
	Namespace* nm;
	std::vector<std::pair<std::string, int64_t>> items;
	
	Ast_Enum_Decl() 
		: Ast_Fundamental_Type(TokenId::UInt32) 
	{
		id = FieldType::Enum;
	}
};

struct Ast_Field_Decl {
	Ast_Type_Decl* type;
	std::string name;
};

struct Ast_Struct_Decl : Ast_Type_Decl {
	int version = -1;
	Namespace* nm;
	std::string name;
	std::vector<Ast_Field_Decl*> fields;
	int size = -1;
	int align = -1;
	bool flat = true;
	bool has_span_class = false;
	uint32_t exception_id;

	bool is_exception() const noexcept { return exception_id != -1; }

	Ast_Struct_Decl() {
		id = FieldType::Struct;
	}
};

struct Ast_MessageDescriptor_Decl : Ast_Type_Decl {
	std::uint32_t message_id;
	std::string name;
	Ast_Struct_Decl* s;

//	bool is_flat() const noexcept override { return s->is_flat(); }

	Ast_MessageDescriptor_Decl() {
		id = FieldType::Message;
	}
};

constexpr auto cft(Ast_Type_Decl* type) noexcept {
	assert(type->id == FieldType::Fundamental);
	return static_cast<Ast_Fundamental_Type*>(type);
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

enum class ArgumentModifier { In, Out };

struct Ast_Function_Argument : Ast_Field_Decl {
	ArgumentModifier modifier;
};

struct Ast_Function_Decl {
	uint16_t idx;
	std::string name;
	Ast_Type_Decl* ret_value;
	Ast_Struct_Decl* in_s;
	Ast_Struct_Decl* out_s;
	Ast_Struct_Decl* ex = nullptr;
	std::vector<Ast_Function_Argument*> args;

	bool is_void() const noexcept { return ret_value->id == FieldType::Void; }
};


using struct_id_t = std::string;

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
};


using AFFAList = List<struct_id_t, Ast_Struct_Decl*>;


inline const std::string& Namespace::name() const noexcept { return name_; }
inline Namespace* Namespace::parent() const noexcept { return parent_; }

inline auto Namespace::push(std::string&& s) noexcept {
	childs_.push_back(new Namespace(this, std::move(s)));
	return childs_.back();
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
	if (auto it = std::find_if(childs_.begin(), childs_.end(),
		[&str](auto nm) {return nm->name() == str; }); it != childs_.end()) {
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

inline std::string Namespace::construct_path(const std::string& delim) const noexcept {
		auto ptr = parent();
	std::string result = name();
	while (ptr && !ptr->name().empty()) {
		result = ptr->name() + delim + result;
		ptr = ptr->parent();
	}
	return result;
}

inline std::string Namespace::to_cpp17_namespace() const noexcept {
	static const auto delim = std::string(2, ':');
	return construct_path(delim);
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
	AFFAList affa_list;
	std::vector<Ast_Struct_Decl*> exceptions;
	uint32_t message_id_last = 0;

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

	Context() {
		nm_root_ = nm_cur_ = new Namespace();
	}
};