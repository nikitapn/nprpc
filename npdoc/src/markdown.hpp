// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <string_view>

namespace npdoc {

// GitHub-flavoured Markdown to HTML. `allow_html` passes raw HTML through;
// leave it off for doc comments, where `vector<Post>` in prose would
// otherwise be read as a tag and vanish.
std::string render_markdown(std::string_view markdown, bool allow_html);

// Like render_markdown for one paragraph, without the enclosing <p>.
std::string render_inline(std::string_view markdown);

} // namespace npdoc
