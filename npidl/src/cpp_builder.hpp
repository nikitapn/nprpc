// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <fstream>
#include <filesystem>
#include "builder.hpp"

class Builder_Cpp : public Builder {
public:
struct _ns {
		Builder_Cpp& bulder_;
		Namespace* nm;
	};
private:
	friend std::ostream& operator<<(std::ostream&, const Builder_Cpp::_ns&);

	std::ofstream oh;
	std::ofstream oc;
	std::ofstream ohm;

	std::filesystem::path file_path_;

	void emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input);
	void emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os);

	void emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input);
	void emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os);

	void assign_from_cpp_type(Ast_Type_Decl* type, std::string op1, std::string op2, std::ostream& os, bool from_iterator = false, bool top_type = false);
	void assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator = false, bool top_object = false);

	void emit_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_flat_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, std::ostream& os);
	void emit_struct2(Ast_Struct_Decl* s, std::ostream& os, bool is_exception);
	void emit_helpers();

	_ns ns(Namespace* nm);
public:
	virtual void emit_struct(Ast_Struct_Decl* s);	
	virtual void emit_exception(Ast_Struct_Decl* s);	
	virtual void emit_using(Ast_Alias_Decl* u);	
	virtual void emit_enum(Ast_Enum_Decl* e);
	virtual void emit_namespace_begin();
	virtual void emit_namespace_end();
	virtual void emit_interface(Ast_Interface_Decl* ifs);
	virtual void emit_file_footer();

	Builder_Cpp(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_inc_path, std::filesystem::path out_src_path);
};
