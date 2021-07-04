// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <fstream>
#include <sstream>
#include <filesystem>
#include "builder.hpp"


class Builder_Typescript : public Builder {
	std::ofstream out;
	std::stringstream spans;

	void emit_accessors(const std::string& flat_name, Ast_Field_Decl* f, int& last_field_ended);
	void emit_type(Ast_Type_Decl* type);
	void emit_span_body(Ast_Struct_Decl* s);
public:
	virtual void emit_struct(Ast_Struct_Decl* s);
	virtual void emit_exception(Ast_Struct_Decl* s);
//	virtual void emit_msg_class(Ast_MessageDescriptor_Decl* m);
//	virtual void emit_message_map(std::vector<Ast_MessageDescriptor_Decl*>& lst) ;
	virtual void emit_handlers(std::vector<Ast_MessageDescriptor_Decl*>& lst);
	virtual void emit_namespace_begin();
	virtual void emit_namespace_end();
	virtual void emit_interface(Ast_Interface_Decl* ifs);
	virtual void emit_file_footer();
	virtual void emit_using(Ast_Alias_Decl* u);
	virtual void emit_enum(Ast_Enum_Decl* e);
	Builder_Typescript(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir);
};