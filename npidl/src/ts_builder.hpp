// Copyright (c) 2021 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include <fstream>
#include <sstream>
#include <filesystem>
#include "builder.hpp"


class TypescriptBuilder : public Builder {
public:
struct _ns {
		TypescriptBuilder& bulder_;
		Namespace* nm;
	};
private:
	friend std::ostream& operator<<(std::ostream&, const TypescriptBuilder::_ns&);

	std::ofstream out;
	std::stringstream spans;

	void emit_parameter_type_for_proxy_call_r(AstTypeDecl* type, std::ostream& os, bool input);
	void emit_parameter_type_for_proxy_call(AstFunctionArgument* arg, std::ostream& os);

	void emit_parameter_type_for_servant_callback_r(AstTypeDecl* type, std::ostream& os, const bool input);
	void emit_parameter_type_for_servant_callback(AstFunctionArgument* arg, std::ostream& os);

	void assign_from_ts_type(AstTypeDecl* type, std::string op1, std::string op2, bool from_iterator = false);
	void assign_from_flat_type(AstTypeDecl* type, std::string op1, std::string op2, 
		bool from_iterator = false, bool top_object = true, bool direct = false);

	void emit_accessors(const std::string& flat_name, AstFieldDecl* f, int& last_field_ended);
	void emit_type(AstTypeDecl* type, std::ostream& os);
	void emit_flat_type(AstTypeDecl* type, std::ostream& os);
	void emit_struct2(AstStructDecl* s, bool is_exception);
	void emit_struct_helpers();
	void emit_variable(AstTypeDecl* type, std::string name, std::ostream& os);

	_ns ns(Namespace* nm);

	auto emit_type(AstTypeDecl* type) {
		return OstreamWrapper{[type, this](std::ostream& os) { this->emit_type(type, os); }};
	}

	auto emit_flat_type(AstTypeDecl* type) {
		return OstreamWrapper{[type, this](std::ostream& os) { this->emit_flat_type(type, os); }};
	}


public:
	virtual void emit_constant(const std::string& name, AstNumber* number);
	virtual void emit_struct(AstStructDecl* s);
	virtual void emit_exception(AstStructDecl* s);
	virtual void emit_namespace_begin();
	virtual void emit_namespace_end();
	virtual void emit_interface(AstInterfaceDecl* ifs);
	virtual void emit_file_footer();
	virtual void emit_using(AstAliasDecl* u);
	virtual void emit_enum(AstEnumDecl* e);

	TypescriptBuilder(Context& ctx, std::filesystem::path file_path, std::filesystem::path out_dir);
};
