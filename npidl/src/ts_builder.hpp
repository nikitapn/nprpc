// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <fstream>
#include <sstream>
#include <filesystem>
#include "builder.hpp"


class Builder_Typescript : public Builder {
public:
struct _ns {
		Builder_Typescript& bulder_;
		Namespace* nm;
	};
private:
	friend std::ostream& operator<<(std::ostream&, const Builder_Typescript::_ns&);

	std::ofstream out;
	std::stringstream spans;

	void emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input);
	void emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os);

	void emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input);
	void emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os);

	void assign_from_ts_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator = false);
	void assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, 
		bool from_iterator = false, bool top_object = true, bool direct = false);

	void emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, int& last_field_ended);
	void emit_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_flat_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_struct2(Ast_Struct_Decl* s, bool is_exception);

	_ns ns(Namespace* nm);
public:
	virtual void emit_struct(Ast_Struct_Decl* s);
	virtual void emit_exception(Ast_Struct_Decl* s);
	virtual void emit_namespace_begin();
	virtual void emit_namespace_end();
	virtual void emit_interface(Ast_Interface_Decl* ifs);
	virtual void emit_file_footer();
	virtual void emit_using(Ast_Alias_Decl* u);
	virtual void emit_enum(Ast_Enum_Decl* e);

	Builder_Typescript(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir);
};