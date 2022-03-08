// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "ast.hpp"
#include <memory>
#include <array>
#include <functional>
#include <algorithm>

class Builder {
protected:
	Context& ctx_;
	bool always_full_namespace_ = false;

	void always_full_namespace(bool flag) { always_full_namespace_ = flag; }
	void make_arguments_structs(Ast_Function_Decl* fn);
	void emit_arguments_structs(std::function<void(Ast_Struct_Decl*)> fn);
public:
	virtual void emit_struct(Ast_Struct_Decl* s) = 0;
	virtual void emit_exception(Ast_Struct_Decl* s) = 0;
	virtual void emit_namespace_begin() = 0;
	virtual void emit_namespace_end() = 0;
	virtual void emit_interface(Ast_Interface_Decl* ifs) = 0;
	virtual void emit_file_footer() = 0;
	virtual void emit_using(Ast_Alias_Decl* u) = 0;
	virtual void emit_enum(Ast_Enum_Decl* e) = 0;
	
	Builder(Context& ctx) : ctx_(ctx) {}
	virtual ~Builder() = default;
};

class BuildGroup {
	Context& ctx_;
	size_t size_;
	static constexpr size_t N = 2;
	std::array<std::unique_ptr<Builder>, N> builders_;
public:
	template<typename F, typename... Args>
	void emit(F fptr, Args&&... args) {
		auto mf = std::mem_fn(fptr);
		std::for_each(builders_.begin(), builders_.begin() + size_, 
			[&](auto& ptr) { mf(ptr.get(), std::forward<Args>(args)...); }
		);
	}

	template<typename T, typename... Args>
	void add(Args&&... args) {
		assert(size_ < N);
		builders_[size_++] = std::make_unique<T>(ctx_, std::forward<Args>(args)...);
	}

	BuildGroup(Context& ctx) : ctx_{ ctx }, size_{ 0 } {}
};