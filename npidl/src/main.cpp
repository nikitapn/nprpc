// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <filesystem>
#include <iostream>

#include <boost/program_options.hpp>

#include "lsp_server.hpp"
#include "parser_interfaces.hpp"
#include "colored_cout.h"

using namespace npidl;

int main(int argc, char* argv[])
{
  namespace po = boost::program_options;
  namespace clr = nprpc::clr;

  std::filesystem::path output_dir;
  std::vector<std::filesystem::path> input_files;
  bool generate_cpp;
  bool generate_typescript;
  bool generate_swift;

  // Declare the supported options.
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("version", "print version information")
    ("lsp", "run as Language Server Protocol server")
    ("cpp", po::bool_switch(&generate_cpp)->default_value(false), "Generate C++")
    ("ts", po::bool_switch(&generate_typescript)->default_value(false),"Generate TypeScript")
    ("swift", po::bool_switch(&generate_swift)->default_value(false), "Generate Swift")
    ("output-dir", po::value<std::filesystem::path>(&output_dir), "Output directory for all generated files")
    ("input-files", po::value<std::vector<std::filesystem::path>>(&input_files), "List of input files");

  po::positional_options_description p;
  p.add("input-files", -1);

  try {
    po::variables_map vm;
    po::store(
        po::command_line_parser(argc, argv).options(desc).positional(p).run(),
        vm);
    po::notify(vm);

    if (vm.count("help")) {
      std::cout << desc << "\n";
      return 0;
    }

    if (vm.count("version")) {
      std::cout << "npidl version 0.1.0\n";
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
    std::cerr << e.what() << '\n';
    return -1;
  }

  try {
    CompilationBuilder builder;
    builder.set_input_files(input_files)
        .set_output_dir(output_dir);
    if (generate_cpp)
      builder.with_language_cpp();
    if (generate_typescript)
      builder.with_language_ts();
    if (generate_swift)
      builder.with_language_swift();

    builder.build()->compile();

    return 0;
  } catch (parser_error& e) {
    std::cerr << clr::red << "Parser error in:\n\t" << clr::cyan << e.file_path
              << ':' << e.line << ':' << e.col << ": " << clr::reset << e.what()
              << '\n';
  } catch (lexical_error& e) {
    std::cerr << clr::red << "Lexer error in:\n\t" << clr::cyan << e.file_path
              << ':' << e.line << ':' << e.col << ": " << clr::reset << e.what()
              << '\n';
  } catch (std::exception& ex) {
    std::cerr << clr::red << "Error: " << clr::reset << ex.what() << '\n';
  }

  return -1;
}