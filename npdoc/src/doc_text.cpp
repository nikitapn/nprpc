// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include "doc_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace npdoc {

namespace {

std::vector<std::string_view> split_lines(std::string_view text)
{
  std::vector<std::string_view> lines;
  while (true) {
    const auto nl = text.find('\n');
    lines.push_back(text.substr(0, nl));
    if (nl == std::string_view::npos)
      break;
    text.remove_prefix(nl + 1);
  }
  return lines;
}

std::string_view ltrim(std::string_view s)
{
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  return s;
}

std::string_view rtrim(std::string_view s)
{
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.remove_suffix(1);
  return s;
}

std::string_view trim(std::string_view s) { return rtrim(ltrim(s)); }

bool is_blank(std::string_view s) { return trim(s).empty(); }

bool is_fence(std::string_view line)
{
  const auto t = ltrim(line);
  return t.starts_with("```") || t.starts_with("~~~");
}

size_t indent_of(std::string_view line)
{
  return line.size() - ltrim(line).size();
}

// Joins lines, dropping blank lines at both ends.
std::string join_trimmed(const std::vector<std::string>& lines)
{
  size_t b = 0, e = lines.size();
  while (b < e && is_blank(lines[b]))
    ++b;
  while (e > b && is_blank(lines[e - 1]))
    --e;
  std::string out;
  for (size_t i = b; i < e; ++i) {
    if (i != b)
      out += '\n';
    out += lines[i];
  }
  return out;
}

// Appends a continuation line to a one-paragraph field.
void append_continuation(std::string& field, std::string_view line)
{
  const auto t = trim(line);
  if (t.empty())
    return;
  if (!field.empty())
    field += '\n';
  field += t;
}

bool is_word_char(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
         c == ':' || c == '.' || c == '(' || c == ')' || c == '<' ||
         c == '>' || c == '~';
}

// `@p x` / `\c x` style inline commands; the argument is the next word.
std::string rewrite_inline_commands(std::string_view line)
{
  std::string out;
  size_t i = 0;
  bool in_code = false;
  while (i < line.size()) {
    const char c = line[i];
    if (c == '`') {
      in_code = !in_code;
      out += c;
      ++i;
      continue;
    }
    if (!in_code && (c == '@' || c == '\\') && i + 1 < line.size()) {
      const auto rest = line.substr(i + 1);
      auto cmd_end = rest.find_first_of(" \t");
      const auto cmd = rest.substr(0, cmd_end);
      const bool code_cmd = cmd == "p" || cmd == "c" || cmd == "a" || cmd == "ref";
      const bool em_cmd = cmd == "e" || cmd == "em";
      if ((code_cmd || em_cmd) && cmd_end != std::string_view::npos) {
        auto arg = ltrim(rest.substr(cmd_end));
        size_t n = 0;
        while (n < arg.size() && is_word_char(arg[n]))
          ++n;
        // Sentence punctuation after the word is not part of it.
        while (n > 0 && (arg[n - 1] == '.' || arg[n - 1] == ':' ||
                         arg[n - 1] == ')'))
          --n;
        if (n > 0) {
          const auto word = arg.substr(0, n);
          if (code_cmd)
            out.append("`").append(word).append("`");
          else
            out.append("*").append(word).append("*");
          i = static_cast<size_t>(arg.data() + n - line.data());
          continue;
        }
      }
    }
    out += c;
    ++i;
  }
  return out;
}

// A block command at the start of a line: `@param`, `\return`, ...
struct BlockCommand {
  std::string_view name;
  std::string_view rest;
};

std::optional<BlockCommand> block_command(std::string_view line)
{
  const auto t = ltrim(line);
  if (t.size() < 2 || (t[0] != '@' && t[0] != '\\'))
    return std::nullopt;
  auto body = t.substr(1);
  size_t n = 0;
  while (n < body.size() && std::isalpha(static_cast<unsigned char>(body[n])))
    ++n;
  if (n == 0)
    return std::nullopt;
  return BlockCommand{body.substr(0, n), ltrim(body.substr(n))};
}

std::string_view callout_label(std::string_view cmd)
{
  static constexpr std::array<std::pair<std::string_view, std::string_view>, 11>
      labels{{{"note", "Note"},
              {"warning", "Warning"},
              {"attention", "Attention"},
              {"see", "See also"},
              {"sa", "See also"},
              {"throws", "Throws"},
              {"throw", "Throws"},
              {"exception", "Throws"},
              {"pre", "Precondition"},
              {"post", "Postcondition"},
              {"deprecated", "Deprecated"}}};
  for (const auto& [k, v] : labels)
    if (k == cmd)
      return v;
  return {};
}

} // namespace

std::string strip_comment_markers(std::string_view raw)
{
  const bool block = ltrim(raw).starts_with("/*");
  std::vector<std::string> out;
  for (auto line : split_lines(raw)) {
    auto t = ltrim(line);
    if (block) {
      if (t.starts_with("/**") || t.starts_with("/*!"))
        t.remove_prefix(3);
      else if (t.starts_with("/*"))
        t.remove_prefix(2);
      t = rtrim(t);
      if (t.ends_with("*/"))
        t.remove_suffix(2);
      // ` * text` continuation style; `*/` alone was removed above.
      if (t.starts_with('*'))
        t.remove_prefix(1);
    } else {
      if (t.starts_with("///<") || t.starts_with("//!<"))
        t.remove_prefix(4);
      else if (t.starts_with("///") || t.starts_with("//!"))
        t.remove_prefix(3);
      else if (t.starts_with("//"))
        t.remove_prefix(2);
    }
    // One space separates the marker from the text; more is indentation.
    if (t.starts_with(' '))
      t.remove_prefix(1);
    out.emplace_back(rtrim(t));
  }
  return join_trimmed(out);
}

DocParts parse_doxygen(std::string_view text)
{
  DocParts parts;
  std::vector<std::string> body;
  // Where continuation lines of the current block command go.
  std::string* sink = nullptr;
  bool in_fence = false;

  for (auto line : split_lines(text)) {
    if (is_fence(line)) {
      in_fence = !in_fence;
      sink = nullptr;
      body.emplace_back(line);
      continue;
    }
    if (in_fence) {
      body.emplace_back(line);
      continue;
    }
    if (is_blank(line)) {
      sink = nullptr;
      body.emplace_back();
      continue;
    }

    const auto cmd = block_command(line);
    if (cmd && (cmd->name == "param" || cmd->name == "tparam")) {
      auto rest = cmd->rest;
      if (rest.starts_with('[')) // [in], [out], [in,out]
        rest = ltrim(rest.substr(std::min(rest.find(']') + 1, rest.size())));
      const auto sp = rest.find_first_of(" \t");
      Param p;
      p.name = std::string(rest.substr(0, sp));
      p.doc = sp == std::string_view::npos
                  ? std::string()
                  : rewrite_inline_commands(trim(rest.substr(sp)));
      if (cmd->name == "tparam") {
        // Template parameters are not call parameters; keep them readable
        // in the body.
        body.push_back("- Template parameter `" + p.name + "`: " + p.doc);
        sink = &body.back();
      } else {
        parts.params.push_back(std::move(p));
        sink = &parts.params.back().doc;
      }
      continue;
    }
    if (cmd && (cmd->name == "return" || cmd->name == "returns" ||
                cmd->name == "result")) {
      parts.returns = rewrite_inline_commands(cmd->rest);
      sink = &*parts.returns;
      continue;
    }
    if (cmd && cmd->name == "retval") {
      const auto sp = cmd->rest.find_first_of(" \t");
      std::string entry = "`" + std::string(cmd->rest.substr(0, sp)) + "`";
      if (sp != std::string_view::npos)
        entry += ": " + rewrite_inline_commands(trim(cmd->rest.substr(sp)));
      if (!parts.returns)
        parts.returns.emplace();
      if (!parts.returns->empty())
        *parts.returns += '\n';
      *parts.returns += entry;
      sink = &*parts.returns;
      continue;
    }
    if (cmd && (cmd->name == "brief" || cmd->name == "details")) {
      body.push_back(rewrite_inline_commands(cmd->rest));
      sink = nullptr;
      continue;
    }
    if (cmd) {
      if (const auto label = callout_label(cmd->name); !label.empty()) {
        body.push_back("**" + std::string(label) + ":** " +
                       rewrite_inline_commands(cmd->rest));
        sink = &body.back();
        continue;
      }
    }

    if (sink) {
      append_continuation(*sink, rewrite_inline_commands(line));
      continue;
    }
    body.push_back(rewrite_inline_commands(line));
  }

  parts.doc = join_trimmed(body);
  return parts;
}

DocParts parse_swift(std::string_view text)
{
  DocParts parts;
  std::vector<std::string> body;
  std::string* sink = nullptr;
  size_t sink_indent = 0;
  // Inside a `- Parameters:` list; its items are `  - name: text`.
  bool in_param_list = false;
  size_t list_indent = 0;
  bool in_fence = false;

  auto starts_ci = [](std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size())
      return false;
    for (size_t i = 0; i < prefix.size(); ++i)
      if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i])
        return false;
    return true;
  };

  for (auto line : split_lines(text)) {
    if (is_fence(line)) {
      in_fence = !in_fence;
      sink = nullptr;
      in_param_list = false;
      body.emplace_back(line);
      continue;
    }
    if (in_fence) {
      body.emplace_back(line);
      continue;
    }
    if (is_blank(line)) {
      sink = nullptr;
      in_param_list = false;
      body.emplace_back();
      continue;
    }

    const auto indent = indent_of(line);
    const auto t = ltrim(line);
    const bool bullet = t.starts_with("- ") || t.starts_with("* ");
    const auto item = bullet ? ltrim(t.substr(2)) : std::string_view{};

    if (bullet && in_param_list && indent > list_indent) {
      const auto colon = item.find(':');
      if (colon != std::string_view::npos) {
        parts.params.push_back(
            {std::string(trim(item.substr(0, colon))), {}, {},
             std::string(trim(item.substr(colon + 1)))});
        sink = &parts.params.back().doc;
        sink_indent = indent;
        continue;
      }
    }
    if (bullet && starts_ci(item, "parameters:")) {
      in_param_list = true;
      list_indent = indent;
      sink = nullptr;
      continue;
    }
    if (bullet && starts_ci(item, "parameter ")) {
      const auto rest = ltrim(item.substr(10));
      const auto colon = rest.find(':');
      if (colon != std::string_view::npos) {
        parts.params.push_back({std::string(trim(rest.substr(0, colon))), {},
                                {}, std::string(trim(rest.substr(colon + 1)))});
        sink = &parts.params.back().doc;
        sink_indent = indent;
        in_param_list = false;
        continue;
      }
    }
    if (bullet && starts_ci(item, "returns:")) {
      parts.returns = std::string(trim(item.substr(8)));
      sink = &*parts.returns;
      sink_indent = indent;
      in_param_list = false;
      continue;
    }

    if (sink && indent > sink_indent && !bullet) {
      append_continuation(*sink, line);
      continue;
    }
    sink = nullptr;
    in_param_list = false;
    body.emplace_back(line);
  }

  parts.doc = join_trimmed(body);
  return parts;
}

std::string summary_of(std::string_view markdown)
{
  std::string out;
  for (auto line : split_lines(markdown)) {
    if (is_blank(line))
      break;
    if (is_fence(line))
      break;
    if (!out.empty())
      out += ' ';
    out += trim(line);
  }
  return out;
}

} // namespace npdoc
