# Let's Encrypt with certbot

How to get and renew Let's Encrypt certificates for nprpc backends sitting
behind [npquicrouter](../npquicrouter/README.md), which routes by SNI and does
**not** terminate TLS.

## The topology

```
                              ┌──────────────────────────────┐
   :80  ──── HTTP-01 ────────►│ npquicrouter                 │
                              │  acme_webroot=/var/www/acme  │
   :443 TCP ─ TLS passthrough►│  SNI → backend               │
   :443 UDP ─ QUIC passthrough│                              │
                              └───┬──────────────────────┬───┘
                                  │                      │
                    site1.nikitapn.com        site2.nikitapn.com
                    (nprpc, own cert)         (nprpc, own cert)
```

Each hostname maps to exactly one backend, and each backend terminates TLS
with a certificate for its own name. Nothing is shared, so there is no
contention between backends over who owns a certificate.

## There is no "last one wins"

A common worry with this layout is that several backends renewing the same
name would somehow invalidate each other's certificates. They would not.
Let's Encrypt has no notion of a domain's *current* certificate: issuance
mints an independent artifact bound to whatever keypair the requester
generated, and nothing but explicit revocation ever invalidates one. Two
backends issuing for the same name end up with two simultaneously valid
certificates and both work.

What *does* bite is rate limiting — in particular the duplicate-certificate
limit on an identical set of FQDNs. Check the [current
limits](https://letsencrypt.org/docs/rate-limits/) before pointing several
issuers at one name; when you get limited, the renewal that fails is the one
whose certificate expires next.

None of that applies to the topology above, where each name has exactly one
issuer. It matters only if you later put several backends behind one
hostname, or want a wildcard shared across all of them — in which case issue
once centrally with DNS-01 and distribute, rather than letting each backend
issue for itself.

## Why HTTP-01 through the router

npquicrouter cannot answer a `tls-alpn-01` challenge, because it does not
terminate TLS. It does own port 80, which makes it the natural place to
answer `http-01` for every hostname it routes: the token only ever has to
exist on the router, and the backends stay out of issuance entirely.

Set `acme_webroot` in the router config to the directory you hand certbot:

```json
{
  "http_redirect_port": 80,
  "acme_webroot": "/var/www/acme"
}
```

With that set, `GET /.well-known/acme-challenge/<token>` is served from disk
and everything else still gets its `301` to HTTPS. See the router's
[ACME HTTP-01](../npquicrouter/README.md#acme-http-01) section for the path
handling and token validation.

The router runs as `www-data` in the sample systemd unit while certbot runs
as root, so create the webroot readable by both:

```bash
sudo install -d -o root -g www-data -m 0755 /var/www/acme
sudo install -d -o root -g www-data -m 0755 /var/www/acme/.well-known
sudo install -d -o root -g www-data -m 0775 /var/www/acme/.well-known/acme-challenge
```

## Certificate reload in nprpc

nprpc reads its certificate and key when a listener starts. Since a renewal
happens roughly every 60 days and would otherwise go unnoticed until the old
certificate expires, the files can be re-read in place:

```cpp
#include <nprpc/nprpc.hpp>

bool ok = nprpc::reload_certificates();
```

This covers every listener that terminates TLS with those files:

| Listener | What reload does |
|---|---|
| HTTP/1.1 + WebSocket | builds a new `ssl::context` and publishes it; the next accepted connection picks it up |
| HTTP/3 | each worker reloads its own `SSL_CTX` on its own io_context thread |
| QUIC RPC (`with_quic`) | builds a new MsQuic configuration for connections accepted from now on |

Connections already established keep the certificate they handshook with —
reload is not a disconnect. A subsystem whose files fail to parse keeps its
previous certificate rather than dropping to none, so a truncated or
half-written renewal cannot take TLS down; the call returns `false` and logs
which subsystem refused.

`reload_certificates()` reads files and allocates, so call it from a normal
thread — an `asio::signal_set` handler is fine, a raw POSIX signal handler is
not.

### Driving it from a certbot deploy hook

Wire `SIGHUP` to the reload in your server:

```cpp
boost::asio::io_context signal_ioc;
boost::asio::signal_set signals(signal_ioc, SIGHUP);

std::function<void(const boost::system::error_code&, int)> on_hup =
    [&](const boost::system::error_code& ec, int) {
      if (ec)
        return;
      nprpc::reload_certificates();
      signals.async_wait(on_hup);   // re-arm
    };
signals.async_wait(on_hup);
```

Then issue and renew with a hook that signals the right backend:

```bash
certbot certonly --webroot -w /var/www/acme \
  -d site1.nikitapn.com \
  --deploy-hook 'systemctl kill -s HUP site1'
```

Renewals reuse the hook recorded at issuance, so `certbot renew` (the timer
Debian and Arch install by default) needs no further configuration.

### Or let nprpc watch the files

When certbot cannot conveniently run a hook — it renews inside a container,
or writes to a shared volume — nprpc can poll the certificate and key instead
and reload when either changes:

```cpp
auto* rpc = nprpc::RpcBuilder()
                .with_http(443)
                    .root_dir("/srv/site1")
                    .ssl("/etc/letsencrypt/live/site1.nikitapn.com/fullchain.pem",
                         "/etc/letsencrypt/live/site1.nikitapn.com/privkey.pem")
                    .watch_certificates(std::chrono::minutes(10))
                    .enable_http3()
                .build();
```

Polling is off by default. It compares mtime and size on both files, so a
same-second rewrite of a PEM is still noticed. Ten minutes is a reasonable
interval — the deadline being met is measured in days, not seconds.

A `--deploy-hook` is still the better mechanism where it is available: it
reloads at the moment the new certificate lands, with no polling and no
window in which the old one is still being served.

### From Swift

The Swift binding exposes the same two mechanisms.

```swift
let builder = RpcBuilder()
let http = builder.withHttp(443)
http.rootDir("/app/www")
http.ssl(certFile: "/certs/live/nikitapn.com/fullchain.pem",
         keyFile:  "/certs/live/nikitapn.com/privkey.pem")
http.enableHttp3()
// Optional: poll instead of (or alongside) a signal. 0 = off, the default.
http.watchCertificates(intervalSeconds: 600)

let rpc = try builder.build()
try rpc.startThreadPool(4)
```

`Rpc.reloadCertificates()` is the on-demand call. Wire it to `SIGHUP` next to
the existing `SIGINT` handler — note the `signal(SIGHUP, SIG_IGN)`, which
`DispatchSource` requires so the default action does not kill the process
first:

```swift
let hup = DispatchSource.makeSignalSource(signal: SIGHUP, queue: .main)
hup.setEventHandler {
    let ok = rpc.reloadCertificates()
    print("certificate reload: \(ok ? "ok" : "FAILED")")
}
signal(SIGHUP, SIG_IGN)
hup.resume()
```

Unlike `SIGINT`, the handler does not re-arm anything — a `DispatchSource`
signal source keeps firing for every signal until cancelled.

## Backends in Docker

A container changes only how the certificate gets in and how the signal gets
out. Both are straightforward.

**Getting the certificate in.** Mount the host's `/etc/letsencrypt`
read-only and point the server at the paths *inside* the container:

```
-v /etc/letsencrypt:/certs:ro
-e NSCALC_PUBLIC_KEY=/certs/live/nikitapn.com/fullchain.pem
-e NSCALC_PRIVATE_KEY=/certs/live/nikitapn.com/privkey.pem
```

Mount the whole tree, not just `live/`. certbot's `live/*.pem` are relative
symlinks into `../../archive/`, so a mount of `live/` alone leaves them
dangling inside the container. With the full tree mounted, the host's certbot
renews and the container sees the new files immediately — no copying, no
image rebuild.

**Getting the signal in.** The renewal runs on the host, so the deploy hook
signals the container:

```bash
certbot certonly --webroot -w /var/www/acme \
  -d nikitapn.com \
  --deploy-hook 'docker kill -s HUP nscalc-swift'
```

`docker kill -s HUP` delivers to PID 1 in the container. That reaches the
server as long as the entrypoint `exec`s it rather than leaving a shell as
PID 1 — a shell would receive the signal and drop it. If the entrypoint ends
in `exec /app/NScalcServer "${ARGS[@]}"`, you are fine.

Sanity-check what PID 1 actually is before relying on it:

```bash
docker exec nscalc-swift ps -o pid,comm -p 1
```

**Or skip the signal entirely.** With `watchCertificates`, nothing has to
cross the container boundary — the server notices the mounted files changing
on its own. That is the lower-friction option when the container is
restarted or renamed often enough that a hook naming it would go stale, and
it works unchanged if you later move renewal into its own container. The
watcher stats through symlinks, so certbot repointing `live/fullchain.pem`
at a new `archive/fullchain2.pem` registers as a change.

### Pointing at Let's Encrypt output

certbot writes `fullchain.pem` and `privkey.pem` into
`/etc/letsencrypt/live/<name>/` as symlinks into `archive/`, and replaces the
symlinks on renewal. Point nprpc at the `live/` paths, not at `archive/`.

`live/` and `archive/` are root-only by default. Either run the backend with
enough privilege to read them, or give the hook the job of copying the pair
somewhere the service user can read and signalling afterwards:

```bash
--deploy-hook 'install -o site1 -m 0600 \
    /etc/letsencrypt/live/site1.nikitapn.com/privkey.pem /etc/site1/tls.key && \
  install -o site1 -m 0644 \
    /etc/letsencrypt/live/site1.nikitapn.com/fullchain.pem /etc/site1/tls.crt && \
  systemctl kill -s HUP site1'
```

Copying the key first and the certificate second still leaves a moment where
the two do not match. That is harmless here: a reload that lands in that
window fails its mismatch check, keeps the previous pair, and the `HUP` after
both copies reloads a consistent one.

## Checking what is actually being served

```bash
# HTTP/1.1 / TLS over TCP
echo | openssl s_client -connect site1.nikitapn.com:443 \
       -servername site1.nikitapn.com 2>/dev/null \
  | openssl x509 -noout -subject -dates

# HTTP/3 over QUIC
curl -sv --http3-only https://site1.nikitapn.com/ 2>&1 | grep -E 'subject:|expire'
```

Both should show the new dates immediately after a reload, without the
process having restarted.
