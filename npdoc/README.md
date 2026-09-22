# npdoc

Collects the public API of NPRPC and its doc comments into one JSON file,
`api.json`, for the documentation site. It reads three sources:

| Source | Input | How |
|---|---|---|
| C++ headers | `include/nprpc/**` (minus `impl/`) | libclang: raw comment, access, pretty-printed declaration |
| Swift package | `NPRPC.symbols.json` | symbol graph the Swift compiler writes during a build |
| IDL | `<file>.doc.json` | `npidl --doc-json` ([format](../npidl/docs/DOC_COMMENTS.md)) |

```sh
just docs-api                      # everything -> <build>/docs/api.json
cmake --build <build> --target docs_api   # C++ and IDL only, unless
                                          # NPRPC_DOCS_SWIFT_SYMBOLS is set
```

npdoc is built when libclang (`clang-c/Index.h`) and Boost.ProgramOptions are
found. Otherwise CMake skips it with a status message.

## What counts as public

- **C++:** public members and namespace-scope declarations in the given
  header roots. Skipped: `detail`/`impl` namespaces, names starting with `_`,
  `= delete`, undocumented `= default`, and forward declarations.
- **Swift:** the symbol graph is emitted at `public`. Skipped:
  compiler-synthesized members, `_` names, symbols with no source location
  (C declarations re-exported through C++ interop), and `Generated/`, which
  is npidl output the IDL entries already cover.
- **IDL:** every declaration npidl reports, with fields, enum items and
  methods as children.

Undocumented symbols are kept, so the site can list them and
`--list-undocumented` can report them.

## Output

A flat list sorted by `id`. The tree is rebuilt through `parent`. Rerunning
on unchanged sources gives a byte-identical file.

```jsonc
{
  "format": 1,
  "symbols": [{
    "id": "cpp:nprpc::PoaBuilder::with_dispatch_executor(DispatchExecutor)",
    "lang": "cpp",                  // cpp | swift | idl
    "kind": "method",               // normalized across languages
    "name": "with_dispatch_executor",
    "qualified": "nprpc::PoaBuilder::with_dispatch_executor",
    "parent": "cpp:nprpc::PoaBuilder",
    "signature": "PoaBuilder &with_dispatch_executor(DispatchExecutor ex)",
    "doc": "Route servant dispatch through `ex` (post+wait). ...",
    "summary": "first paragraph on one line",
    "params": [{ "name": "ex", "type": "DispatchExecutor", "doc": "" }],
    "returns": "...",               // when documented
    "file": "include/nprpc/nprpc.hpp",
    "line": 277
  }]
}
```

`doc` is Markdown. Each language's parameter and return markup is moved into
`params` and `returns`:

- **Doxygen:** `@param`, `@return`, `@retval`. It also rewrites `@p x` and
  `@c x` to `` `x` ``, `@brief` to plain text, and `@note`/`@warning`/`@throws`
  to bold callouts.
- **Swift:** `- Parameter x:`, `- Parameters:` lists, and `- Returns:`.
- **IDL:** already structured, since npidl records each parameter's doc
  separately.

Ids are `<lang>:<qualified name>`. C++ functions append their parameter
types, plus ` const`, so overloads stay distinct. Swift keeps argument labels
(`withHttp(_:)`) and, when two overloads still collide, appends the tail of
the compiler's mangled name.
