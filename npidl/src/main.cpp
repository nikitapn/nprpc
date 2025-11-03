// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#include <iostream>
#include <filesystem>

#include <boost/program_options.hpp>
#include <nplib/utils/colored_cout.h>

#include "parser_interface.hpp"
#include "lsp_server.hpp"

using namespace npidl;

int main(int argc, char* argv[]) {
  namespace po = boost::program_options;

  std::filesystem::path output_dir;
  std::vector<std::filesystem::path> input_files;
  bool generate_cpp;
  bool generate_typescript;

  // Declare the supported options.
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("lsp", "run as Language Server Protocol server")
    // There is no option to disable C++ generation for now, as it's the primary target
    // And CppBuilder is implicitly required for TypeScript generation, since it generates auxiliary structs
    ("cpp", po::bool_switch(&generate_cpp)->default_value(true), "Generate C++")
    ("ts", po::bool_switch(&generate_typescript)->default_value(false), "Generate TypeScript")
    ("output-dir", po::value<std::filesystem::path>(&output_dir), "Output directory for all generated files")
    ("input-files", po::value<std::vector<std::filesystem::path>>(&input_files), "List of input files")
    ;

  po::positional_options_description p;
  p.add("input-files", -1);

  try {
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << "\n";
      return 0;
    }

    // LSP mode - run Language Server
    if (vm.count("lsp")) {
      LspServer server;
      server.run();
      return 0;
    }

    if (!vm.count("input-files")) {
      std::cerr << "Input files not specified.\n";
      return -1;
    }
  } catch (po::unknown_option& e) {
    std::cerr << e.what();
    return -1;
  }

  try {
    CompilationBuilder builder;
    builder
      .set_input_files(input_files)
      .set_output_dir(output_dir)
      .with_language_cpp()
      ;
    if (generate_typescript)
      builder.with_language_ts();

    builder.build()->compile();

    return 0;
  } catch (parser_error& e) {
    std::cerr << clr::red << "Parser error in:\n\t" << clr::cyan << e.file_path << ':' << e.line << ':' << e.col << ": " << clr::reset << e.what() << '\n';
  } catch (lexical_error& e) {
    std::cerr << clr::red << "Lexer error in:\n\t" << clr::cyan << e.file_path << ':' << e.line << ':' << e.col << ": " << clr::reset << e.what() << '\n';
  } catch (std::exception& ex) {
    std::cerr << ex.what() << '\n';
  }

  return -1;
}