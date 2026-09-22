# NPRPC docs site

The documentation site, served by NPRPC itself. It's a page handler that
renders Mustache templates from `api.json`, with htmx for navigation and
search. There's no JavaScript build and no Node.

```sh
just docs-api      # C++ headers + Swift package + IDL + docs/*.md -> <build>/docs/api.json
just docs-serve    # http://localhost:8080
```

`api.json` is reloaded when it changes, so rerunning `just docs-api` shows up
on the next request. A broken file keeps the last good copy and logs why.
`docs-serve` also re-reads templates on every request.

## Layout

| Path | What |
|---|---|
| `Sources/DocsModel` | `api.json` → pages, URLs, anchors, cross-links, search. No NPRPC dependency; tested in `Tests/`. |
| `Sources/DocsWeb` | routing, view models, template loading |
| `Sources/docs-server` | the executable |
| `templates/` | Mustache; `layout` wraps every full page |
| `web/` | static root: `style.css`, `vendor/htmx.min.js` |

## URLs

- `/api/<lang>`: every top-level declaration in `idl`, `swift` or `cpp`,
  grouped by namespace.
- `/api/<lang>/<qualified path>`: one page per top-level declaration, and per
  anything with members, e.g. `/api/cpp/nprpc/PoaBuilder`. Members appear on
  their parent's page under an anchor (`#with_dispatch_executor`). Overloads
  of a free function share a page.
- `/guide/<name>`: `docs/<name>.md`. Links between guides are rewritten to
  these URLs.
- `/search?q=`: the header box fetches a short list as you type, and
  submitting the form shows the full results page.

Every link is boosted by htmx. The server answers an `HX-Request` with the
page body alone, and `#content` is swapped in place. A request without htmx,
or a history restore, gets the whole page.

Inline code in docs that names exactly one symbol, like `` `PoaBuilder` `` or
`` `PoaBuilder::build()` ``, becomes a link to it. Doc comments resolve names
within their own language. Guides resolve across all languages, and only link
a name that is unique among them.

## Configuration

| Variable | Default |
|---|---|
| `DOCS_API` | `../../.build_relwith_debinfo/docs/api.json` |
| `DOCS_ROOT` | current directory (holds `templates/` and `web/`) |
| `DOCS_PORT` | `8080` |
| `DOCS_TLS_CERT`, `DOCS_TLS_KEY` | unset: plain HTTP |
| `DOCS_TEMPLATE_RELOAD` | unset; `1` re-reads templates per request |
