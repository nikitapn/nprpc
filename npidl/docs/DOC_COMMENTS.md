# Documentation comments

npidl keeps `///` comments and attaches them to the declaration that follows.
From there they go three places:

- **Generated code** in each language's native form, so IDE hover works on
  the stubs: `///` in C++ and Swift, `/** */` in TypeScript.
- **`--doc-json`** writes `<file>.doc.json`: every declaration with its doc
  and its signature spelled in IDL. This is the input for documentation
  tooling.
- **LSP hover** shows the doc under the usual type summary, both on the
  declaration and on any reference to the type.

## Syntax

```npidl
/// A blog post as shown in listings.
///
/// The text is Markdown and is passed through verbatim.
message Post {
  /// Stable slug, used in URLs.
  slug: string;
  title: string;
}

/// Read access to the blog.
[trusted]
interface Blog {
  /// Fetch one post.
  void get_post(
    /// The post slug.
    slug: in string,
    /// Receives the post.
    post: out Post
  ) raises(NotFound);
}
```

Rules:

- Exactly three slashes. `////` and longer lines are separators and are
  ignored, as are `//` and `/* */`.
- The `///` must start its line. A trailing `/// ...` after code is an ordinary
  comment; otherwise it would silently attach to the *next* declaration.
- Consecutive lines join with newlines. One space after `///` is dropped;
  deeper indentation is kept, so Markdown code blocks and lists work.
- Put the doc above any attribute list (`[trusted]`, `[unreliable]`).

Documentable declarations: `message`, `exception`, their fields, `enum` and
each enum item, `alias`, `one of` variants, `interface`, methods, and method
parameters. `const` declarations and individual variant arms don't carry docs
yet.

## Generated output

Parameter docs are merged into the method's comment in each language's form:

| Language | Parameter | Lone `out` argument of a `void` method |
|---|---|---|
| C++ | `@param name text` | stays `@param` (it's a reference parameter) |
| Swift | `- Parameter name: text` | `- Returns: text` (Swift returns it) |
| TypeScript proxy | `@param name text` | stays `@param` (`NPRPC.ref<T>`) |
| TypeScript `http` | `@param name text` | `@returns text` (the promise resolves to it) |

Docs go on user-facing types only: never on the `flat::` wire structs or the
synthesized argument structs.

Two escapes keep the comment from changing the code around it:

- In a JSDoc block, `*/` is written as `*\/`.
- In `///` output, trailing backslashes are dropped. A `\` at the end of a C++
  `//` comment splices the next generated line into the comment. This costs
  Markdown's backslash hard line break.

## `--doc-json` format

```sh
npidl --doc-json --output-dir out idl/live_blog.npidl   # -> out/live_blog.doc.json
```

It can be combined with `--cpp`/`--ts`/`--swift` in one run.

```jsonc
{
  "format": 1,
  "file": "live_blog.npidl",
  "module": "live_blog",
  "declarations": [
    // Every declaration has: kind, name, namespace, file, line, doc.
    // "file" is where it was declared, which differs for imports.
    { "kind": "message",   "fields": [{ "name", "type", "line", "doc" }] },
    { "kind": "exception", "fields": [...] },      // __ex_id is omitted
    { "kind": "enum",      "underlying": "u32",
                           "items": [{ "name", "value", "doc" }] },
    { "kind": "alias",     "target": "vector<Post>" },
    { "kind": "variant",   "arms": [{ "name", "type" }] },
    { "kind": "interface", "trusted": false, "bases": ["Base"],
      "methods": [{ "name", "line", "doc", "returns",
                    "stream": "" | "server" | "client" | "bidi",
                    "unreliable": false,
                    "params": [{ "name", "direction": "in" | "out",
                                 "direct", "type", "doc" }],
                    "raises": ["NotFound"] }] },
    { "kind": "const", "name", "namespace", "file", "value" }  // no line/doc
  ]
}
```

Types are spelled as in IDL source (`string?`, `vector<Post>`,
`bidi_stream<In, Out>`), using the same formatter as LSP hover.
