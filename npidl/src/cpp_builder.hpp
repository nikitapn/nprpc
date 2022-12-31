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
	std::ofstream ocpp;
	std::stringstream oc;

	std::filesystem::path file_path_;

	std::unordered_map<Ast_Function_Decl*, std::string> proxy_arguments_;

	BlockDepth bd;

	void emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input);
	void emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os);

	void emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input);
	void emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os);

	void assign_from_cpp_type(Ast_Type_Decl* type, std::string op1, std::string op2, std::ostream& os, bool from_iterator = false, bool top_type = false, bool direct_type = false);
	void assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, std::ostream& os, bool from_iterator = false, bool top_object = false);

	void emit_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_flat_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_direct_type(Ast_Type_Decl* type, std::ostream& os);

	void emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, std::ostream& os);


	enum class Target { Regular, Exception, FunctionArgument };
	void emit_struct2(Ast_Struct_Decl* s, std::ostream& os, Target target);
	void emit_helpers();
	void emit_safety_checks();
	void emit_safety_checks_r(Ast_Type_Decl* type, std::string op, std::ostream& os, bool from_iterator = false, bool top_type = false);
	

	void proxy_call(Ast_Function_Decl* fn);
	void proxy_async_call(Ast_Function_Decl* fn);
	std::string_view proxy_arguments(Ast_Function_Decl* fn);
	static void emit_function_arguments(
		Ast_Function_Decl* fn,
		std::ostream& os,
		std::function<void(Ast_Function_Argument*, std::ostream& os)> emitter
	);

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
