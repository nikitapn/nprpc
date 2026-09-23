# NPRPC docs site

The documentation site, served by NPRPC itself. It's a page handler that
renders Mustache templates from `api.json`, with htmx for navigation and
search. There's no JavaScript build and no Node; the vendored scripts are served as they are.

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
| `web/` | static root: `style.css`, `code.js` (syntax highlighting, including an npidl grammar), `vendor/` (htmx; highlight.js 11.11.1 core and language modules, unmodified) |

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
| `DOCS_HOSTNAME` | `localhost` |
| `DOCS_TLS_CERT`, `DOCS_TLS_KEY` | unset: plain HTTP. `SIGHUP` re-reads them. |
| `DOCS_HTTP3` | unset; `1` also serves HTTP/3 (needs TLS) |
| `DOCS_SHM_CHANNEL` | unset; a name takes HTTP/3 through npquicrouter's shared-memory rings `/nprpc_<name>_c2s` / `_s2c` instead of loopback UDP. The deploy script sets it (`--shm-channel`, `--no-shm`) and mounts the router's directory as `/dev/shm` |
| `DOCS_TEMPLATE_RELOAD` | unset; `1` re-reads templates per request |

`SIGINT` and `SIGTERM` stop the server.

## Deploying

`deploy/deploy.sh` publishes the site to a Docker host behind
[npquicrouter](../../npquicrouter/README.md). It builds `api.json` and a
release `docs-server` (compiled in `nprpc-dev`), builds an image on the
shared [runtime image](../DOCKER_RUNTIME_IMAGE.md), and runs it on the
server's loopback, where the router forwards the hostname's TCP and UDP.

```sh
docs/site/deploy/deploy.sh --ssh debian@nikitapn.com
docs/site/deploy/deploy.sh --help     # hostname, port, certificate options
```

It needs `nprpc-dev` and an `nprpc-runtime` built from it
(`just build-dev-image`, `just build-runtime-image`), and refuses to deploy if
the two differ. The runtime image is copied to the server only when the
server doesn't have it.

The container runs with a read-only filesystem and no capabilities except
binding port 443, publishes its port on `127.0.0.1` only, and restarts unless
stopped.

### A new hostname, once

For `nprpc.nikitapn.com` (defaults shown; change them with the options):

1. **DNS:** an `A` record (and `AAAA` for IPv6) for the name, pointing at
   the server.
2. **Router:** add a route to npquicrouter's config and restart it:
   ```json
   { "sni": "nprpc.nikitapn.com", "tcp_backend": "127.0.0.1:9443", "udp_backend": "127.0.0.1:9443",
     "shm_ingress_channel": "nprpc_docs", "shm_egress_channel": "nprpc_docs",
     "shm_ingress_ring_kib": 512, "shm_egress_ring_kib": 1024 }
   ```
3. **Certificate:** once DNS resolves, issue it through the router's ACME
   webroot. The deploy hook makes the running server reload it on renewal:
   ```sh
   sudo certbot certonly --webroot -w /var/www/acme -d nprpc.nikitapn.com \
     --deploy-hook 'docker kill -s HUP nprpc-docs'
   ```
4. **Deploy** with the script above.

See [CERTBOT.md](../CERTBOT.md) for the router's ACME setup.
