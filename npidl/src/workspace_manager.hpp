// Copyright (c) 2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by LICENSING file in the topmost directory

#pragma once

#include "ast.hpp"
#include "null_builder.hpp"
#include "parse_for_lsp.hpp"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <filesystem>
#include <vector>

namespace npidl {

// Represents a single project context (group of related files)
// Files are related if they import each other
struct ProjectContext {
  std::unique_ptr<Context> ctx;
  std::unique_ptr<builders::BuildGroup> builder;
  
  // All files that are part of this project
  std::unordered_set<std::string> file_uris;
  
  // Import graph: file -> files it imports
  std::unordered_map<std::string, std::vector<std::string>> import_graph;
  
  // Reverse import graph: file -> files that import it
  std::unordered_map<std::string, std::vector<std::string>> reverse_imports;
  
  ProjectContext() 
    : ctx(std::make_unique<Context>("<project>"))
    , builder(std::make_unique<builders::BuildGroup>(ctx.get()))
  {
    // NullBuilder for LSP (no code generation)
    builder->add<builders::NullBuilder>();
  }
  
  // Add a file to this project and update import graph
  void add_file(const std::string& uri, const std::vector<std::string>& imports) {
    file_uris.insert(uri);
    import_graph[uri] = imports;
    
    // Update reverse imports
    for (const auto& imported : imports) {
      reverse_imports[imported].push_back(uri);
    }
  }
  
  // Check if this project contains a file
  bool contains(const std::string& uri) const {
    return file_uris.find(uri) != file_uris.end();
  }
  
  // Check if a file is related to this project (imports or is imported by)
  bool is_related_to(const std::string& uri, const std::vector<std::string>& file_imports) const {
    // Check if this file imports any file in the project
    for (const auto& imported : file_imports) {
      if (contains(imported)) {
        return true;
      }
    }
    
    // Check if any file in the project imports this file
    auto it = reverse_imports.find(uri);
    if (it != reverse_imports.end() && !it->second.empty()) {
      return true;
    }
    
    return false;
  }
  
  // Remove file from project
  void remove_file(const std::string& uri) {
    file_uris.erase(uri);
    
    // Clean up import graph
    if (auto it = import_graph.find(uri); it != import_graph.end()) {
      for (const auto& imported : it->second) {
        auto& rev = reverse_imports[imported];
        rev.erase(std::remove(rev.begin(), rev.end(), uri), rev.end());
      }
      import_graph.erase(it);
    }
    
    // Clean up reverse imports
    reverse_imports.erase(uri);
  }
};

// Manages multiple project contexts
class WorkspaceManager {
public:
  // Find or create project for a file
  // Returns the project this file belongs to
  ProjectContext* get_project_for_file(const std::string& uri, const std::vector<std::string>& imports);
  
  // Remove a file from workspace
  void remove_file(const std::string& uri);
  
  // Get project that contains this file (or nullptr)
  ProjectContext* find_project(const std::string& uri);
  
private:
  // List of all projects in the workspace
  std::vector<std::unique_ptr<ProjectContext>> projects_;
  
  // Quick lookup: uri -> project index
  std::unordered_map<std::string, size_t> file_to_project_;
  
  // Find project that this file is related to
  ProjectContext* find_related_project(const std::string& uri, const std::vector<std::string>& imports);
};

} // namespace npidl
