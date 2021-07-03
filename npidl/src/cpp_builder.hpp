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
	std::string module_name_;
	const std::string base_name_;
	std::filesystem::path file_path_;

	int m_struct_n_ = 0;
	bool always_full_namespace_ = false;

	void emit_parameter_type_for_proxy_call_r(Ast_Type_Decl* type, std::ostream& os, bool input);
	void emit_parameter_type_for_proxy_call(Ast_Function_Argument* arg, std::ostream& os);

	void emit_parameter_type_for_servant_callback_r(Ast_Type_Decl* type, std::ostream& os, const bool input);
	void emit_parameter_type_for_servant_callback(Ast_Function_Argument* arg, std::ostream& os);

	void emit_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_flat_type(Ast_Type_Decl* type, std::ostream& os);
	void emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, std::ostream& os);
	void emit_struct(Ast_Struct_Decl* s, std::ostream& os, bool is_exception);
	void assign_from_cpp_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator = false);
	void assign_from_flat_type(Ast_Type_Decl* type, std::string op1, std::string op2, bool from_iterator = false);
	void make_arguments_structs(Ast_Function_Decl* fn);
	_ns ns(Namespace* nm);
	void always_full_namespace(bool flag) { always_full_namespace_ = flag; }
public:
	virtual void emit_struct(Ast_Struct_Decl* s);	
	virtual void emit_exception(Ast_Struct_Decl* s);	
	virtual void emit_using(Ast_Alias_Decl* u);	
	virtual void emit_enum(Ast_Enum_Decl* e);
//	virtual void emit_msg_class(Ast_MessageDescriptor_Decl* m, std::ostream& os);
//	virtual void emit_message_map(std::vector<Ast_MessageDescriptor_Decl*>& lst);
	virtual void emit_handlers(std::vector<Ast_MessageDescriptor_Decl*>& lst);
	virtual void emit_namespace_begin();
	virtual void emit_namespace_end();
	virtual void emit_interface(Ast_Interface_Decl* ifs);
	virtual void emit_file_footer();

	Builder_Cpp(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_inc_path, std::filesystem::path out_src_path);
};
