// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>

#include <boost/program_options.hpp>

#include "lsp_server.hpp"
#include "parser_interfaces.hpp"
#include "colored_cout.h"

using namespace npidl;

namespace {

// Copies clog/cerr to a file while still writing stderr (stdout is LSP JSON).
class TeeBuf : public std::streambuf
{
  std::streambuf* a_;
  std::streambuf* b_;

public:
  TeeBuf(std::streambuf* a, std::streambuf* b)
      : a_(a)
      , b_(b)
  {
  }

protected:
  int overflow(int c) override
  {
    if (c == EOF)
      return !EOF;
    if (a_->sputc(static_cast<char>(c)) == EOF)
      return EOF;
    if (b_ && b_->sputc(static_cast<char>(c)) == EOF)
      return EOF;
    return c;
  }

  int sync() override
  {
    const int r1 = a_->pubsync();
    const int r2 = b_ ? b_->pubsync() : 0;
    return (r1 == 0 && r2 == 0) ? 0 : -1;
  }

  std::streamsize xsputn(const char* s, std::streamsize n) override
  {
    const auto n1 = a_->sputn(s, n);
    if (b_)
      b_->sputn(s, n);
    return n1;
  }
};

class LspFileLog
{
  std::ofstream file_;
  TeeBuf tee_;
  std::streambuf* old_clog_;
  std::streambuf* old_cerr_;
  bool redirected_ = false;

public:
  explicit LspFileLog(const std::filesystem::path& path)
      : file_(path, std::ios::out | std::ios::app)
      , tee_(std::cerr.rdbuf(), file_ ? file_.rdbuf() : nullptr)
      , old_clog_(std::clog.rdbuf())
      , old_cerr_(std::cerr.rdbuf())
  {
    if (!file_) {
      std::cerr << "Failed to open LSP log file: " << path.string() << '\n';
      return;
    }
    file_ << std::unitbuf;
    std::cerr.rdbuf(&tee_);
    std::clog.rdbuf(&tee_);
    redirected_ = true;
    std::clog << "LSP log file: " << path.string() << std::endl;
  }

  ~LspFileLog()
  {
    if (redirected_) {
      std::clog.rdbuf(old_clog_);
      std::cerr.rdbuf(old_cerr_);
    }
  }
};

} // namespace

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
    ("lsp-log", po::value<std::filesystem::path>(),
     "append LSP logs to this file (default: /tmp/npidl-lsp.log)")
    ("stdio", "ignored; LSP always uses stdin/stdout (accepted for VS Code)")
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
      // Must happen before any I/O and before installing the log tee:
      // calling this later can reset rdbuf() and drop the file log.
      std::ios::sync_with_stdio(false);
      std::cin.tie(nullptr);
      std::cout.tie(nullptr);

      const auto log_path = vm.count("lsp-log")
                                ? vm["lsp-log"].as<std::filesystem::path>()
                                : std::filesystem::path("/tmp/npidl-lsp.log");
      LspFileLog log(log_path);
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