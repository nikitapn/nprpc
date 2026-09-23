# NPRPC Runtime Docker Image

`nprpc-runtime` is the base image for running NPRPC applications in
production. It holds what a built application needs at run time and nothing
else:

- the Swift 6.3.3 runtime (it is based on `swift:6.3.3-slim`)
- `libnprpc` and `libmsquic`, without debug info
- the Boost shared libraries
- `liburing2`, `libatomic1`, `ca-certificates` and `tzdata`

The image is about 320 MB, roughly 30 MB more than `swift:6.3.3-slim`.
Applications are compiled in the [development image](DOCKER_DEV_IMAGE.md),
and their production images are built `FROM nprpc-runtime`.

## Why a shared base

Every image built from the same `nprpc-runtime` shares its layers. Docker
stores them once on the host, and because every container maps the same
library files, the kernel loads `libnprpc` into memory once for all of them.
Build the runtime image once per NPRPC version and reuse it for every site on
a host. If each application copied the libraries into its own image instead,
each would have its own copy on disk and in memory.

## Building

```bash
just build-dev-image          # if nprpc-dev is not built yet
just build-runtime-image      # from nprpc-dev:latest
```

The image is tagged `nprpc-runtime:<version>-<revision>` and
`nprpc-runtime:latest`. The revision is the NPRPC commit recorded in the dev
image, or the dev image's id for dev images built before the commit label
existed.

To build from another dev image, for example an application's builder image
derived from `nprpc-dev`:

```bash
just build-runtime-image myapp-builder:latest
```

## Matching the build image

An application must run on a runtime image made from the same NPRPC build it
was compiled against. The Swift bridge is linked into the application itself,
while `libnprpc` comes from the runtime image. If the two differ, the library
misreads the application's configuration, and the process fails in confusing
ways (for example, a QUIC error from a server that never enabled QUIC).

Each runtime image records the sha256 of its `libnprpc` in the label
`io.nprpc.libnprpc.sha256`. Deploy scripts should compare it with the
library in their build image and refuse a mismatch:

```bash
runtime=$(docker image inspect --format \
  '{{index .Config.Labels "io.nprpc.libnprpc.sha256"}}' nprpc-runtime:1.0.0-abc)
builder=$(docker run --rm --entrypoint sha256sum myapp-builder:latest \
  /opt/nprpc/lib/libnprpc.so.1.0.0 | cut -d' ' -f1)
[ "$runtime" = "$builder" ] || { echo "runtime/builder mismatch" >&2; exit 1; }
```

When NPRPC changes, rebuild the dev image, the application's builder image
and the runtime image together, then redeploy each application. Until an
application is redeployed, it keeps running on its old runtime tag, and both
runtime images stay on the host.

## Using it

```dockerfile
# Pin the versioned tag, not latest, so a newer runtime shipped for another
# application does not change this one.
ARG NPRPC_RUNTIME_IMAGE=nprpc-runtime:1.0.0-abc
FROM ${NPRPC_RUNTIME_IMAGE}

COPY MyServer /app/MyServer
COPY www /app/www
WORKDIR /app
ENTRYPOINT ["/app/MyServer"]
```

`LD_LIBRARY_PATH` already points at `/opt/nprpc/lib`, `/opt/boost/lib` and
the Swift runtime. Libraries only your application uses go in your own image.

## Shipping to a server

```bash
just ship-runtime-image debian@example.com nprpc-runtime:1.0.0-abc
```

This does nothing if the server already has that exact image. Otherwise it
streams the image over ssh with `docker save | docker load`, which keeps the
layer digests, so images the server builds `FROM` it share its layers.
Pushing to a registry works as well.

Images for one tag are built from the same layers, so a server that builds
several applications from `nprpc-runtime:1.0.0-abc` holds the libraries once.
Remove old runtime tags with `docker image rm` once no container uses them.
