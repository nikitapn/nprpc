// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <exception>
#include <cassert>
#include <array>
#include <utility>
#include <stack>
#include <type_traits>
#include <functional>
#include "ast.hpp"
#include "cpp_builder.hpp"
#include "ts_builder.hpp"
#include "utils.hpp"
#include <boost/program_options.hpp>

#include <charconv>
#include <variant>

struct Token {
	TokenId id;
	std::string name;
	std::string_view static_name;
	
	bool operator == (TokenId id) const noexcept {
		return this->id == id;
	}

	bool is_fundamental_type() const noexcept {
		return static_cast<int>(id) >= fundamental_type_first && 
			static_cast<int>(id) <= fundamental_type_last;
	}

	std::string_view to_string_view() const noexcept {
		if (id == TokenId::Name) return std::string_view(name);
		if (static_cast<int>(id) < fundamental_type_first) {
			return std::string_view((char*)&id, 1);
		} else {
			return static_name;
		}
	}
};

std::ostream& operator<<(std::ostream& os, const Token& t) {
	os << "id: " << static_cast<int>(t.id) << " n: " << t.to_string_view();
	return os;
}


class lexical_error : public std::runtime_error {
public:
	const int line;
	const int col;

	lexical_error(int _line, int _col, const char* msg)
		: std::runtime_error(msg)
		, line(_line)
		, col(_col)
	{
	}

	lexical_error(int _line, int _col, const std::string& msg)
		: std::runtime_error(msg)
		, line(_line)
		, col(_col)
	{
	}
};

class parser_error : public lexical_error {
public:
	parser_error(int _line, int _col, const char* msg)
		: lexical_error(_line, _col, msg) {}

	parser_error(int _line, int _col, const std::string& msg)
		: lexical_error(_line, _col, msg) {}
};

template<typename Key, typename T, size_t Size>
struct Map {
	const std::pair<Key, T>(&items_)[Size];

	constexpr std::optional<std::pair<Key, T>> find(const Key& key) const noexcept {
		const auto it = std::find_if(items_, items_ + Size,
			[&key](const auto& x) { return x.first == key; });

		if (it != (items_ + Size)) {
			return *it;
		} else {
			return {};
		}
	}
};

class Lexer {
	std::string text_;
	const char* ptr_;
	int line_ = 1;
	int col_ = 1;

	static constexpr bool is_digit(char c) noexcept {
		return c >= '0' && c <= '9';
	}
	static constexpr bool is_letter_or_underscore(char c) noexcept {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	}
	static constexpr bool is_valid_name(char c) noexcept {
		return is_digit(c) || is_letter_or_underscore(c) || c == '_';
	}

	static constexpr bool is_delimeter(char c) noexcept {
		switch (c) {
		case ' ': case '\0': case '\n': case '\t':
		case '{': case '}': case '<': case '>': case '[': case ']': case '(': case ')':
		case ':': case ',': case ';': case '/': case '=':
			return true;
		default:
			return false;
		}
	}

	void skip_wp() noexcept {
		while (*ptr_ == ' ' || *ptr_ == '\n' || *ptr_ == '\t') next();
	}

	void skip_line_comment() {
		while (*ptr_ != '\n' && *ptr_ != '\0') next();
	}

	void skip_comments() {
		while (*ptr_ != '\0' && !(*ptr_ == '*' && *(ptr_ + 1) == '/')) next();
		if (*ptr_ != '\0') {
			ptr_ += 2;
			col_ += 2;
		}
	}

	char cur() const noexcept { return *ptr_; }

	void next() noexcept {
		if (*ptr_ == '\t') {
			col_ += 2;
		} else if (*ptr_ == '\n') {
			col_ = 1;
			++line_;
		} else {
			++col_;
		}
		++ptr_;
	}

	char look() const noexcept {
		return *(ptr_ + 1);
	}

	std::string read_number() {
		const char* begin = ptr_;

		bool hex = false;

		while (!is_delimeter(cur())) {
			if (cur() == 'x') {
				if (hex) throw lexical_error(line_, col_, "Incorrect hexadecimal number.");
				hex = true;
			} else if (hex) {
				auto c = std::tolower(cur());
				if (!(is_digit(c) || (c >= 'a' && c <= 'f'))) {
					throw lexical_error(line_, col_, "Error.");
				}
			}
			else if (!is_digit(cur())) {
				throw lexical_error(line_, col_, "Error.");
			}
			next();
		}
		return std::string(begin, ptr_ - begin);
	}

	Token read_string() {
		assert(is_letter_or_underscore(cur()));

		const char* begin = ptr_;

		while (!is_delimeter(cur())) {
			if (!is_valid_name(cur())) throw lexical_error(line_, col_, "Error.");
			next();
		}

		using namespace std::string_view_literals;

		static constexpr std::pair<std::string_view, TokenId> alphabet[] =
		{
			{ "boolean"sv, TokenId::Boolean },
			{ "i8"sv, TokenId::Int8 },
			{ "u8"sv, TokenId::UInt8 },
			{ "i16"sv, TokenId::Int16 },
			{ "u16"sv, TokenId::UInt16 },
			{ "i32"sv, TokenId::Int32 },
			{ "u32"sv, TokenId::UInt32 },
			{ "i64"sv, TokenId::Int64 },
			{ "u64"sv, TokenId::UInt64 },
			{ "float32"sv, TokenId::Float32 },
			{ "float64"sv, TokenId::Float64},
			{ "vector"sv, TokenId::Vector},
			{ "string"sv, TokenId::String},
			{ "flat"sv, TokenId::Flat},
			{ "message"sv, TokenId::Message},
			{ "of"sv, TokenId::Of},
			{ "namespace"sv, TokenId::Namespace},
			{ "interface"sv, TokenId::Interface},
			{ "object"sv, TokenId::Object},
			{ "void"sv, TokenId::Void},
			{ "in"sv, TokenId::In },
			{ "out"sv, TokenId::Out },
			{ "enum"sv, TokenId::Enum },
			{ "using"sv, TokenId::Using },
			{ "exception"sv, TokenId::Exception },
			{ "raises"sv, TokenId::Raises },
			{ "direct"sv, TokenId::OutDirect },
			{ "class"sv, TokenId::Class },
			{ "extends"sv, TokenId::Extends }
		};

		static constexpr Map<std::string_view, TokenId,
			sizeof(alphabet) / sizeof(std::pair<std::string_view, TokenId>)> map{ alphabet };

		auto const str = std::string_view(begin, ptr_);

		if (auto o = map.find(str); o) {
			return Token{ o.value().second, {}, o.value().first };
		}

		return Token{ TokenId::Name, std::string(str) };
	}
public:
	int col() const noexcept { return col_; }
	int line() const noexcept { return line_; }

	Token	tok() {
		skip_wp();
		switch (cur()) {
		case '\0': return Token{ TokenId::Eof };
		case ':':
			next();
			if (cur() == ':') {
				next();
				return Token{ TokenId::DoubleColon };
			}
			return Token{ TokenId::Colon };
		case ';': next(); return Token{ TokenId::Semicolon };
		case '{': next(); return Token{ TokenId::BracketOpen };
		case '}': next(); return Token{ TokenId::BracketClose };
		case '#': next(); return Token{ TokenId::Hash };
		case '<': next(); return Token{ TokenId::Less };
		case '>': next(); return Token{ TokenId::Greater };
		case '[': next(); return Token{ TokenId::SquareBracketOpen };
		case ']': next(); return Token{ TokenId::SquareBracketClose };
		case '(': next(); return Token{ TokenId::RoundBracketOpen };
		case ')': next(); return Token{ TokenId::RoundBracketClose };
		case ',': next(); return Token{ TokenId::Comma };
		case '=': next(); return Token{ TokenId::Assignment };
		case '/':
			next();
			if (cur() == '/') {
				skip_line_comment();
			} else if (cur() == '*') {
				next();
				skip_comments();
			} else {
				throw lexical_error(line_, col_, "Unknown token '/'.");
			}
			return tok();
		default:
			if (is_digit(cur())) {
				return Token{ TokenId::Number, read_number() };
			} else if (is_letter_or_underscore(cur())) {
				return read_string();
			} else {
				using namespace std::string_literals;
				throw lexical_error(line_, col_, "Unknown token '"s + cur() + "'");
			}
		};
	}
	Lexer(std::filesystem::path file_path) {
		if (!std::filesystem::exists(file_path)) {
			throw std::runtime_error((const char*)(file_path.u8string() + u8" does not exist").c_str());
		}
		std::ifstream ifs(file_path);
		std::noskipws(ifs);
		std::copy(std::istream_iterator<char>(ifs), std::istream_iterator<char>(), std::back_inserter(text_));
		text_ += '\0';
		ptr_ = text_.c_str();
	}
};

template<typename T, size_t MaxSize>
class Queue {
	T* begin_;
	T* end_;
	size_t size_;
	std::aligned_storage_t<sizeof(T), alignof(T)> storage_[MaxSize];

	T* data() noexcept {
		return std::launder(reinterpret_cast<T*>(&storage_[0]));
	}
public:
	void push_back(const T& t) noexcept {
		assert(size() < MaxSize);
		if (end_ == data() + MaxSize) {
			end_ = data();
		}
		new (end_++) T(t);
		size_++;
	}

	void push_back(T&& t) noexcept {
		assert(size() < MaxSize);
		if (end_ == data() + MaxSize) {
			end_ = data();
		}
		new (end_++) T(std::move(t));
		size_++;
	}

	T&& pop_front() noexcept {
		assert(size() > 0);
		if (begin_ == data() + MaxSize) {
			begin_ = data();
		}
		size_--;
		return std::move(*begin_++);
	}

	T& back() noexcept {
		assert(size() > 0);
		return *(end_ - 1);
	}

	size_t size() const noexcept { return size_; }

	void clear() noexcept {
		size_ = 0;
		begin_ = end_ = data();
	}

	T& operator[](size_t ix) {
		int64_t diff = (data() + MaxSize) - (begin_ + ix);
		return diff > 0 ? *(begin_ + ix) : *(data() + ((int64_t)ix + diff));
	}

	Queue() { clear(); }
};

class Parser {
	friend struct PeekGuard;
	Lexer lex_;

	static constexpr int64_t max_lookahead_tok_n = 5;
	Queue<Token, max_lookahead_tok_n> tokens_;

	bool done_ = false;
	size_t tokens_looked_ = 0;
	Context& ctx_;
	BuildGroup& builder_;

	class PeekGuard {
		Parser& parser_;
		size_t saved_;
		bool discard_;
	public:
		void flush() {
			discard_ = true;
			parser_.flush();
		}

		PeekGuard(Parser& parser)
			: parser_(parser)
			, saved_(parser.tokens_looked_)
			, discard_(false)
		{
		}

		~PeekGuard() {
			if (!discard_) parser_.tokens_looked_ = saved_;
		}
	};

	Token& peek() {
		assert(tokens_looked_ < max_lookahead_tok_n);
		Token* tok;
		if (tokens_looked_ >= tokens_.size()) {
			tokens_.push_back(lex_.tok());
			tok = &tokens_.back();
		} else {
			tok = &tokens_[tokens_looked_];
		}
		tokens_looked_++;
		return *tok;
	}
	
	Token match(TokenId id) {
		Token t;

		if (tokens_.size() == 0) {
			t = lex_.tok();
		} else {
			t = tokens_.pop_front();
		}

		if (t != id) {
			throw_unexpected_token(t);
		}

		return t;
	}

	Token match(char id) {
		return match(static_cast<TokenId>(id));
	}

	void flush() {
		tokens_looked_ = 0;
		tokens_.clear();
	}

	void throw_error(const char* msg) {
		throw parser_error(lex_.line(), lex_.col(), msg);
	}

	void throw_error(const std::string& msg) {
		throw parser_error(lex_.line(), lex_.col(), msg);
	}

	void throw_unexpected_token(const Token& tok) {
		throw parser_error(lex_.line(), lex_.col(), 
			"Unexpected token: '" + std::string(tok.to_string_view()) + '\'');
	}

	Ast_Number parse_number(const Token& tok) {
		const auto& str = tok.name;

		std::int64_t i64;
		float flt32;
		double flt64;

		if (str.size() > 2 && str[0] == '0' && str[1] == 'x') {
			auto result = std::from_chars(str.data() + 2, str.data() + str.size(), i64, 16);
			if (result.ec != std::errc()) { 
				throw_error("Hex number.std::from_chars returned: " + std::make_error_code(result.ec).message()); 
			}
			return Ast_Number{ i64, NumberFormat::Hex };
		} else if(str.find('.') != std::string::npos) {
			if (str.back() == 'f') {
				auto result = std::from_chars(str.data(), str.data() + str.size(), flt32);
				if (result.ec != std::errc()) {
					throw_error("Float32 number.std::from_chars returned: " + std::make_error_code(result.ec).message());
				}
				return Ast_Number{ flt32, NumberFormat::Decimal };
			} else {
				auto result = std::from_chars(str.data(), str.data() + str.size(), flt64);
				if (result.ec != std::errc()) {
					throw_error("Float64 number.std::from_chars returned: " + std::make_error_code(result.ec).message());
				}
				return Ast_Number{ flt64, NumberFormat::Decimal };
			}
		} else {
			auto result = std::from_chars(str.data(), str.data() + str.size(), i64, 10);
			if (result.ec != std::errc()) {
				throw_error("Integer number.std::from_chars returned: " + std::make_error_code(result.ec).message());
			}
			return Ast_Number{ i64, NumberFormat::Decimal };
		}
	}

	template<typename T, typename... Args>
	bool check(T f, Args&&... args) {
		size_t saved = tokens_looked_;
		if (std::mem_fn(f)(this, std::forward<Args>(args)...)) return true;
		tokens_looked_ = saved;
		return false;
	}

	bool array_decl(Ast_Type_Decl*& type) {
		if (peek() == TokenId::SquareBracketOpen) {
			flush();

			auto length = std::atoi(match(TokenId::Number).name.c_str());
			match(TokenId::SquareBracketClose);

			type = new Ast_Array_Decl(type, length);

			return true;
		}
		return false;
	}

	bool is_double_colon() {
		return peek() == TokenId::DoubleColon;
	}

	bool get_type_namespace(Namespace*& nm, Token& type) {
		auto parse_namespace = [&]() {
			do {
				type = match(TokenId::Name);
				if (auto next = nm->find_child(type.name); next) {
					match(TokenId::DoubleColon);
					nm = next;
				} else {
					return;
				}
				flush();
			} while (check(&Parser::is_double_colon));
			type = match(TokenId::Name);
		};

		auto first_tok = peek();

		if (first_tok == TokenId::DoubleColon) {
			flush();
			nm = ctx_.nm_root();
			parse_namespace();

			return true;
		}

		auto nm_parent = nm->parent();
		if (!nm_parent) return false;

		if (nm_parent = nm_parent->find_child(first_tok.name)) {
			flush();
			match(TokenId::DoubleColon);
			nm = nm_parent;
			parse_namespace();

			return true;
		}

		return false;
	}

	bool type_decl(Ast_Type_Decl*& type) {
		type = nullptr;

		auto nm = ctx_.nm_cur();
		Token t;

		if (check(&Parser::get_type_namespace, std::ref(nm), std::ref(t))) {
			flush();
		} else {
			t = peek();
		}

		if (t.is_fundamental_type()) {
			flush();
			type = new Ast_Fundamental_Type(t.id);
			check(&Parser::array_decl, std::ref(type));
			return true;
		}

		switch (t.id) {
		case TokenId::Name:
			flush();
			type = nm->find_type(t.name, false);
			if (!type) {
				throw_error("Unknown type '" + t.name + "'");
			}
			check(&Parser::array_decl, std::ref(type));
			return true;
		case TokenId::Vector:
			flush();
			type = new Ast_Vector_Decl();
			match('<');
			if (!check(&Parser::type_decl, std::ref(static_cast<Ast_Vector_Decl*>(type)->type))) throw_error("Expected a type declaration");
			match('>');
			return true;
		case TokenId::String:
			flush();
			type = new Ast_String_Decl();
			check(&Parser::array_decl, std::ref(type));
			return true;
		case TokenId::Void:
			flush();
			type = new Ast_Void_Decl();
			return true;
		case TokenId::Object:
			flush();
			type = new Ast_Object_Decl();
			return true;
		default:
			return false;
		}
	}

	bool field_decl(Ast_Field_Decl*& field) {
		Token field_name;
		Ast_Type_Decl* type;

		if (!(
			(field_name = peek()) == TokenId::Name &&
			peek() == TokenId::Colon &&
			check(&Parser::type_decl, std::ref(type)))) {
			return false;
		}

		field = new Ast_Field_Decl();

		field->name = std::move(field_name.name);
		field->type = type;

		return true;
	}

	bool arg_decl(Ast_Function_Argument& arg) {
		Token arg_name;

		if (!((arg_name = peek()) == TokenId::Name && peek() == TokenId::Colon)) return false;

		if (check(&Parser::one, TokenId::In)) arg.modifier = ArgumentModifier::In;
		else if (check(&Parser::one, TokenId::Out)) arg.modifier = ArgumentModifier::Out;
		else {
			throw_error("Expected 'in' or 'out' keywords after parameter name declaration.");
		}

		if (arg.modifier == ArgumentModifier::Out && check(&Parser::one, TokenId::OutDirect)) {
			arg.direct = true;
		}

		if (!check(&Parser::type_decl, std::ref(arg.type))) return false;

		arg.name = arg_name.name;

		return true;
	}

	bool version_decl(Ast_Struct_Decl* s) {
		if (!(peek() == TokenId::Hash && peek() == TokenId::Name && tokens_.back().name == "version")) return false;

		flush();

		auto vn = match(TokenId::Number);

		if (s->version != -1) {
			throw_error("version redefinition.");
		}

		s->version = std::atoi(vn.name.c_str());
		return true;
	}

	bool struct_close_tag() {
		if (peek() == TokenId::BracketClose) {
			flush();
			return true;
		}
		return false;
	}

	bool struct_decl() {
		Token name_tok;

		if (!((name_tok = peek()) == TokenId::Name && peek() == TokenId::Colon)) return false;

		bool is_exception;

		auto tok = peek();
		if (tok == TokenId::Flat) is_exception = false;
		else if (tok == TokenId::Exception) is_exception = true;
		else return false;

		flush();

		auto s = new Ast_Struct_Decl();

		s->name = name_tok.name;
		
		if (is_exception) {
			s->exception_id = ctx_.next_exception_id();
			ctx_.exceptions.push_back(s);
		} else {
			s->exception_id = -1;
		}

		s->nm = ctx_.nm_cur();

		if (is_exception) {
			auto ex_id = new Ast_Field_Decl();
			ex_id->name = "__ex_id";
			ex_id->type = new Ast_Fundamental_Type(TokenId::UInt32);
			s->fields.push_back(ex_id);
		}

		match('{');

		while (check(&Parser::struct_close_tag) == false) {
			Ast_Field_Decl* field;
			if (check(&Parser::field_decl, std::ref(field))) {
				s->fields.push_back(field);
				s->flat &= is_flat(field->type);
				match(';');
			} else if (check(&Parser::version_decl, s)) {
				// ok
			} else {
				throw_error("Syntax error");
			}
		}

		calc_struct_size_align(s);

		// std::cerr << s->name << ": size " << s->size << ", alignof "<< s->align << '\n';

		builder_.emit((!s->is_exception() ? &Builder::emit_struct : &Builder::emit_exception), s);
		ctx_.nm_cur()->add(s->name, s);

		return true;
	}

	bool namespace_decl() {
		if (peek() != TokenId::Namespace) return false;
		flush();

		auto name = match(TokenId::Name); match('{');
		ctx_.push_namespace(std::move(name.name));

		builder_.emit(&Builder::emit_namespace_begin);

		for (;;) {
			if (check(&Parser::one, TokenId::BracketClose)) {
				builder_.emit(&Builder::emit_namespace_end);
				ctx_.pop_namespace();
				break;
			}
			if (!check(&Parser::stmt_decl)) {
				throw_error("Expected tokens: struct, message descriptor, semicolon, }");
			}
		}
		return true;
	}

	bool function_decl(Ast_Function_Decl*& f) {
		{
			Ast_Type_Decl* ret_type;
			if (!check(&Parser::type_decl, std::ref(ret_type))) return false;
			f = new Ast_Function_Decl();
			f->ret_value = ret_type;
		}

		f->name = match(TokenId::Name).name;
		match('(');

		Ast_Function_Argument arg;

		if (check(&Parser::one, TokenId::RoundBracketClose) == false) {
			for (;;) {
				if (!check(&Parser::arg_decl, std::ref(arg))) {
					throw_error("Expected tokens: argument declaration");
				}
				f->args.push_back(new Ast_Function_Argument(std::move(arg)));
				if (check(&Parser::one, TokenId::RoundBracketClose)) break;
				match(',');
			}
		}

		auto tok = peek();
		
		if (tok == TokenId::Semicolon) {
			flush();
		} else if (tok == TokenId::Raises) {
			flush();
			match('(');
			
			auto type = ctx_.nm_cur()->find_type(match(TokenId::Name).name, false);
			
			if (!type) {
				throw_error("unknown exception type");
			} 
				
			if (type->id != FieldType::Struct && cflat(type)->is_exception()) {
				throw_error("class is not an exception");
			}

			f->ex = cflat(type);

			match(')'); match(';');
		} else {
			throw_unexpected_token(tok);
		}

		return true;
	}

	bool interface_decl() {
		if (peek() != TokenId::Interface) return false;
		flush();

		auto ifs = new Ast_Interface_Decl();
		ifs->name = match(TokenId::Name).name;

		{
			PeekGuard pg(*this);
			if (peek() == TokenId::Colon) {
				pg.flush();

				for (;;) {
					auto name = match(TokenId::Name).name;
					if (auto type = ctx_.nm_cur()->find_type(name, false); type && type->id == FieldType::Interface) {
						ifs->plist.push_back(cifs(type));
					} else {
						throw_error("Unknown interface");
					}
					{
						PeekGuard pg(*this);
						if (peek() == TokenId::Comma) {
							pg.flush();
						} else {
							break;
						}
					}
				}
			}
		}

		match('{');

		Ast_Function_Decl* f;
		for (;;) {
			if (check(&Parser::one, TokenId::BracketClose)) break;
			if (check(&Parser::function_decl, std::ref(f))) {
				ifs->fns.emplace_back(f);
				continue;
			}
			throw_error("Expected tokens: function declaration, '}' ");
		}


		for (uint16_t idx = 0; idx < ifs->fns.size(); ++idx) {
			ifs->fns[idx]->idx = idx;
		}

		ctx_.interfaces.push_back(ifs);
		ctx_.nm_cur()->add(ifs->name, ifs);
		builder_.emit(&Builder::emit_interface, ifs);

		return true;
	}

	bool class_decl() {
		if (peek() != TokenId::Class) return false;
		flush();

		auto ifs = new Ast_Interface_Decl();
		ifs->name = match(TokenId::Name).name;

	}

	bool one(TokenId id) {
		if (peek() == id) { flush(); return true; }
		return false;
	}

	bool eof() {
		if (peek() == TokenId::Eof) { flush(); return (done_ = true); }
		return false;
	}

	bool message_descriptor_decl(std::uint32_t& message_id_last) {
		Token t_name;

		if (!(
			(t_name = peek()) == TokenId::Name &&
			peek() == TokenId::Colon &&
			peek() == TokenId::Message &&
			peek() == TokenId::Of)) {
			return false;
		}

		auto m = new Ast_MessageDescriptor_Decl;
		m->message_id = message_id_last++;
		m->name = t_name.name;

		flush();

		auto s_name = match(TokenId::Name).name;
		auto type = ctx_.nm_cur()->find_type(s_name, true);

		if (!type) {
			throw_error("Unknown type '" + s_name + "'");
		}

		if (type->id != FieldType::Struct) {
			throw_error("Error: type of message is not a flat");
		}

		m->s = static_cast<Ast_Struct_Decl*>(type);

		match(';');

		//builder_.emit_msg_class(m);
		//ctx_.decl_messages.push_back(m);

		return true;
	}

	bool using_decl() {
		Token left;

		if (!(
			peek() == TokenId::Using &&
			(left = peek()) == TokenId::Name &&
			peek() == TokenId::Assignment
			))
		{
			return false;
		}

		Ast_Type_Decl* right;
		if (!check(&Parser::type_decl, std::ref(right))) {
			throw_error("Expected a type declaration after using");
		}

		auto a = new Ast_Alias_Decl(std::move(left.name), ctx_.nm_cur(), right);
		ctx_.nm_cur()->add(a->name, a);

		builder_.emit(&Builder::emit_using, a);

		return true;
	}

	bool enum_decl() {
		if (peek() != TokenId::Enum) return false;
		flush();

		auto e = new Ast_Enum_Decl;

		e->name = std::move(match(TokenId::Name).name);
		e->token_id = TokenId::UInt32;
		e->nm = ctx_.nm_cur();

		{
			PeekGuard g(*this);
			Token tok = peek();
			if (tok == TokenId::Colon) {
				tok = peek();
				if (tok.is_fundamental_type() == false ||
					tok == TokenId::Float32 ||
					tok == TokenId::Float64 ||
					tok == TokenId::Boolean) {
					throw_error("expected a numeric type");
				}
				e->token_id = tok.id;
				g.flush();
			}
		}

		const int max_size = get_fundamental_size(e->token_id);

		match('{');

		Token tok;
		int64_t ix = 0;

		while ((tok = peek()) != TokenId::BracketClose) {
			if (tok.id != TokenId::Name) throw_error("Unexpected token '" + tok.name + '\'');
			
			auto name = std::move(tok.name);

			tok = peek();

			if (tok.id == TokenId::Assignment) {
				flush();
				auto n = parse_number(match(TokenId::Number));
				n.max_size = max_size;

				if (!std::holds_alternative<std::int64_t>(n.value)) {
					throw_error("Floating types are not allowed.");
				}

				// explicit
				e->items.emplace_back(std::move(name), std::pair<Ast_Number, bool>{ n, true });
				
				ix = std::get<std::int64_t>(n.value) + 1;
				tok = peek();
			} else {

				// implicit
				e->items.emplace_back(std::move(name), std::pair<Ast_Number, bool>{ ix, 0 });
				ix++;
			}

			if (tok == TokenId::BracketClose) break;
			if (tok != TokenId::Comma) throw_error("Unexpected token '" + tok.name + '\'');

			flush();
		}

		flush();

		ctx_.nm_cur()->add(e->name, e);

		builder_.emit(&Builder::emit_enum, e);

		return true;
	}

	bool stmt_decl() {
		return (
			check(&Parser::namespace_decl) ||
			check(&Parser::interface_decl) ||
			check(&Parser::struct_decl) ||
			check(&Parser::enum_decl) ||
			check(&Parser::message_descriptor_decl, std::ref(ctx_.message_id_last)) ||
			check(&Parser::using_decl) ||
			check(&Parser::class_decl) ||
			check(&Parser::one, TokenId::Semicolon)
			);
	}

public:
	Parser(std::filesystem::path file_path, Context& ctx, BuildGroup& builder)
		: lex_(file_path)
		, ctx_(ctx)
		, builder_(builder)
	{
	}

	void parse() {
		while (!done_) {
			if (!(
				check(&Parser::stmt_decl) ||
				check(&Parser::eof)
				)) {
				throw_error("Expected tokens: statement, eof");
			}
		}
	}
};


int main(int argc, char* argv[]) {
	namespace po = boost::program_options;
	
	std::filesystem::path out_inc_dir, out_src_dir, out_ts_dir, input_file;
	bool generate_typescript;

	// Declare the supported options.
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("out-inc-dir", po::value<std::filesystem::path>(&out_inc_dir), "directory for generated include files")
		("out-src-dir", po::value<std::filesystem::path>(&out_src_dir), "directory for generated source files")
		("out-ts-dir", po::value<std::filesystem::path>(&out_ts_dir), "directory for generated typescript files")
		("gen-ts", po::value<bool>(&generate_typescript)->default_value(true), "generate typescript")
		("input-file", po::value<std::filesystem::path>(&input_file), "input file")
		;

	po::positional_options_description p;
	p.add("input-file", -1);
	
	try {
		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
		po::notify(vm);
		if (vm.count("help")) {
			std::cout << desc << "\n";
			return 0;
		}
		if (!vm.count("input-file")) {
			std::cerr << "no input file...\n";
			return -1;
		}
	} catch (po::unknown_option& e) {
		std::cerr << e.what();
		return -1;
	}

	try {
		//input_file = std::filesystem::path("c:\\projects\\cpp\\npk-calculator\\server\\idl\\npkcalc.npidl");

		Context ctx(input_file);

		BuildGroup builder(ctx);
		builder.add<Builder_Cpp>(input_file, out_inc_dir, out_src_dir);
		if (generate_typescript) {
			builder.add<Builder_Typescript>(input_file, out_ts_dir);
		}

		Parser parser(input_file, ctx, builder);
		parser.parse();

		builder.emit(&Builder::emit_file_footer);

		return 0;
	} catch (parser_error& e) {
		std::cerr << "Parser error in line: " << e.line << " column: " << e.col << " : " << e.what() << '\n';
	} catch (lexical_error& e) {
		std::cerr << "Lexical error in line: " << e.line << " column: " << e.col << " : " << e.what() << '\n';
	} catch (std::exception& ex) {
		std::cerr << ex.what() << '\n';
	}

	return -1;
}