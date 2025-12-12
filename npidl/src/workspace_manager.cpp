// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "workspace_manager.hpp"
#include <iostream>

namespace npidl {

ProjectContext*
WorkspaceManager::get_project_for_file(const std::string& uri,
                                       const std::vector<std::string>& imports)
{
  // First check if file is already in a project
  auto it = file_to_project_.find(uri);
  if (it != file_to_project_.end()) {
    auto* project = projects_[it->second].get();
    // Update imports in case they changed
    project->add_file(uri, imports);
    return project;
  }

  // Try to find a related project
  auto* related = find_related_project(uri, imports);
  if (related) {
    related->add_file(uri, imports);
    file_to_project_[uri] = std::distance(
        projects_.begin(),
        std::find_if(projects_.begin(), projects_.end(),
                     [related](const auto& p) { return p.get() == related; }));
    std::clog << "Added file " << uri << " to existing project" << std::endl;
    return related;
  }

  // No related project found - create new isolated project
  auto new_project = std::make_unique<ProjectContext>();
  new_project->add_file(uri, imports);

  size_t project_idx = projects_.size();
  file_to_project_[uri] = project_idx;
  projects_.push_back(std::move(new_project));

  std::clog << "Created new project for file " << uri << std::endl;
  return projects_[project_idx].get();
}

void WorkspaceManager::remove_file(const std::string& uri)
{
  auto it = file_to_project_.find(uri);
  if (it == file_to_project_.end()) {
    return;
  }

  size_t project_idx = it->second;
  auto* project = projects_[project_idx].get();

  project->remove_file(uri);
  file_to_project_.erase(it);

  // If project is now empty, remove it
  if (project->file_uris.empty()) {
    std::clog << "Removing empty project at index " << project_idx << std::endl;
    projects_.erase(projects_.begin() + project_idx);

    // Update indices in file_to_project_
    for (auto& [file_uri, idx] : file_to_project_) {
      if (idx > project_idx) {
        idx--;
      }
    }
  }
}

ProjectContext* WorkspaceManager::find_project(const std::string& uri)
{
  auto it = file_to_project_.find(uri);
  if (it == file_to_project_.end()) {
    return nullptr;
  }
  return projects_[it->second].get();
}

ProjectContext*
WorkspaceManager::find_related_project(const std::string& uri,
                                       const std::vector<std::string>& imports)
{
  for (const auto& project : projects_) {
    if (project->is_related_to(uri, imports)) {
      return project.get();
    }
  }

  return nullptr;
}

} // namespace npidl
