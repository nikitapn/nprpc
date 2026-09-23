// Syntax highlighting for code blocks, with highlight.js (vendor/highlight).
//
// Markdown code blocks arrive as <pre><code class="language-X">; signatures
// carry the page's language the same way. Blocks without a language inside
// a declaration's docs take the language of its signature.

import hljs from '/vendor/highlight/core.min.js';
import bash from '/vendor/highlight/languages/bash.min.js';
import cmake from '/vendor/highlight/languages/cmake.min.js';
import cpp from '/vendor/highlight/languages/cpp.min.js';
import dockerfile from '/vendor/highlight/languages/dockerfile.min.js';
import json from '/vendor/highlight/languages/json.min.js';
import swift from '/vendor/highlight/languages/swift.min.js';
import typescript from '/vendor/highlight/languages/typescript.min.js';

// NPRPC IDL. Keywords follow the npidl lexer (npidl/src/library.cpp).
function npidl(hljs) {
  return {
    name: 'NPRPC IDL',
    aliases: ['idl'],
    keywords: {
      keyword:
        'module import namespace interface message exception enum alias const ' +
        'raises in out direct helper stream server_stream client_stream ' +
        'bidi_stream one of',
      type:
        'boolean i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 string vector object void',
      literal: 'true false',
    },
    contains: [
      hljs.COMMENT('///', '$', { className: 'doctag', relevance: 0 }),
      hljs.C_LINE_COMMENT_MODE,
      hljs.C_BLOCK_COMMENT_MODE,
      hljs.QUOTE_STRING_MODE,
      hljs.C_NUMBER_MODE,
      // [unreliable], [force_helpers=1], [trusted=false]
      { className: 'meta', begin: /\[[A-Za-z_]\w*(=[^\]]*)?\]/ },
      {
        // interface Calculator, message Post, ...
        match: [/\b(?:interface|message|exception|enum)\b/, /\s+/, /[A-Za-z_]\w*/],
        className: { 1: 'keyword', 3: 'title.class' },
      },
    ],
  };
}

for (const [name, lang] of Object.entries({
  bash, cmake, cpp, dockerfile, json, swift, typescript, npidl,
})) {
  hljs.registerLanguage(name, lang);
}

const langOf = (el) =>
  [...el.classList].find((c) => c.startsWith('language-'))?.slice(9);

function highlight(root) {
  for (const el of root.querySelectorAll('pre > code')) {
    if (el.dataset.highlighted) continue;
    let lang = langOf(el);
    if (!lang) {
      const sig = el.closest('.decl')?.querySelector('.signature > code');
      lang = sig && langOf(sig);
    }
    if (!lang || !hljs.getLanguage(lang)) continue;
    el.classList.add('language-' + lang);
    hljs.highlightElement(el);
  }
}

highlight(document);
// htmx swaps in page bodies without a reload; highlight each new one.
document.addEventListener('htmx:afterSwap', (e) => highlight(e.detail.target));
