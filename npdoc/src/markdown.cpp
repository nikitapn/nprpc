// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "markdown.hpp"

#include <md4c-html.h>

#include <stdexcept>

namespace npdoc {

std::string render_markdown(std::string_view markdown, bool allow_html)
{
  std::string out;
  unsigned flags = MD_DIALECT_GITHUB;
  if (!allow_html)
    flags |= MD_FLAG_NOHTML;
  const int rc = md_html(
      markdown.data(), static_cast<MD_SIZE>(markdown.size()),
      [](const MD_CHAR* text, MD_SIZE size, void* data) {
        static_cast<std::string*>(data)->append(text, size);
      },
      &out, flags, 0);
  if (rc != 0)
    throw std::runtime_error("markdown rendering failed");
  return out;
}

std::string render_inline(std::string_view markdown)
{
  auto html = render_markdown(markdown, false);
  while (!html.empty() && html.back() == '\n')
    html.pop_back();
  if (html.starts_with("<p>") && html.ends_with("</p>"))
    html = html.substr(3, html.size() - 7);
  return html;
}

} // namespace npdoc
