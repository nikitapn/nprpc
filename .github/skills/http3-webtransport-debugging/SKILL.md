---
name: http3-webtransport-debugging
description: 'Debug HTTP/3 and WebTransport integration issues with nghttp3/ngtcp2 in nprpc. Use when: Chromium or Chrome fails with ERR_QUIC_PROTOCOL_ERROR, WebTransportError Opening handshake failed, WebTransport sessions close immediately, browser-only HTTP/3 fails while curl works, CONNECT /wt is never handled, or when validating nghttp3 SETTINGS changes for WebTransport.'
argument-hint: 'Describe the failure signature, client, and endpoint you are testing'
---

# HTTP/3 And WebTransport Debugging

## When To Use

- Integrating browser WebTransport with the nghttp3/ngtcp2 backend.
- Debugging differences between curl HTTP/3 success and Chromium failure.
- Investigating whether a failure is caused by certificate trust, HTTP/3 negotiation, or WebTransport semantics.
- Validating changes in `src/http/http3_server_nghttp3.cpp` or vendored `third_party/nghttp3`.
- Checking whether a settings or transport-parameter patch is actually required.

## Repo-Specific Facts

- In this repo, `enable_http3()` uses the same port as HTTP. The browser-facing HTTP/3 authority is `listen_http_port`, not the separate native QUIC transport port from `.with_quic(...)`.
- The extra `.with_quic(...)` transport is for native NPRPC QUIC clients, not browser WebTransport.
- Browser WebTransport in Chromium is stricter than curl for both certificate handling and HTTP/3 capability advertisement.
- Manual browser navigation to an HTTPS origin can fail for certificate-trust reasons even when the WebTransport path works with `serverCertificateHashes`.
- The focused browser test is `test/js/test/webtransport-browser.test.ts`.

## Main Procedure

### 1. Classify The Failure First

Start by deciding which bucket the symptom belongs to:

- `ERR_QUIC_PROTOCOL_ERROR` on page navigation or `page.goto(...)`
- `WebTransportError: Opening handshake failed`
- WebTransport session opens and then closes immediately
- Browser times out while curl `--http3-only` works
- Server never logs or handles `CONNECT /wt`

Do not patch protocol logic until the failure class is clear.

### 2. Confirm The Target Port And Transport Model

Verify the browser is pointed at the HTTP port, not the native QUIC port.

Check these files:

- `include/nprpc/impl/nprpc_impl.hpp`
- `src/http/http_server.cpp`
- `src/http/http3_server_nghttp3.cpp`
- `test/src/common/helper.inl`

Confirm all of the following:

- `http3_enabled` means HTTP/3 on the same port as HTTP.
- Alt-Svc and HTTP/3 bootstrap use `listen_http_port`.
- Any `.with_quic(...)` port is not being confused with the browser HTTP/3 authority.

### 3. Separate Certificate Problems From Protocol Problems

If manual Chromium navigation fails, capture whether QUIC/TLS is being rejected before blaming HTTP/3 or WebTransport.

Recommended checks:

1. Test plain HTTP/3 with curl:
   - `curl --http3-only --insecure -I https://host:port/`
2. Reproduce in Chromium with QUIC forced to the authority.
3. If needed, capture a Chrome netlog.

Interpretation:

- curl fails too: listener, port, ALPN, or basic HTTP/3 setup is broken.
- curl works but Chromium fails with certificate verification: fix certificate trust or `serverCertificateHashes` handling first.
- curl works and Chromium trusts the cert but WebTransport still fails: continue with HTTP/3/WebTransport-specific checks.

### 4. Use A Browser Netlog For Browser-Only Failures

When curl works but Chromium does not, capture a Chromium netlog instead of inferring behavior from server logs alone.

Look for:

- QUIC certificate verification errors
- received SETTINGS identifiers
- whether the browser targets `https://host:port/wt`
- whether the WebTransport client ever leaves `CONNECTING`
- whether the session closes because of server-side FIN or handshake rejection

This is the fastest way to distinguish:

- cert rejection
- unsupported SETTINGS
- missing capability advertisement
- bad WebTransport CONNECT stream behavior

### 5. Validate Plain HTTP/3 Before Debugging WebTransport

Once certificate issues are ruled out, confirm that basic HTTP/3 is usable.

Expected outcome:

- `curl --http3-only` returns a valid HTTP/3 response on the HTTP port.

If plain HTTP/3 does not work reliably, do not proceed to WebTransport-specific debugging.

### 6. Verify The Browser Reaches CONNECT `/wt`

The WebTransport path is not working until you can prove the browser is attempting the extended CONNECT request.

Check for evidence that Chromium is targeting:

- `https://host:port/wt`
- `:method = CONNECT`
- `:protocol = webtransport-h3` or compatible token

If the browser never reaches `/wt`, keep debugging negotiation and capability advertisement.

### 7. Check The Server Receive Path For Extended CONNECT Semantics

Browser WebTransport CONNECT is not a normal request/response flow.

In this repo, validate these points in `src/http/http3_server_nghttp3.cpp`:

- CONNECT handling must not wait for request end-of-stream.
- The server must respond when headers are complete, not only when `end_stream` fires.
- The CONNECT response must keep the stream open.
- WebTransport child streams must be treated separately from normal HTTP request streams.

Critical rule:

- If `start_response()` only runs from `end_stream`, browser WebTransport CONNECT will hang.

### 8. Check CONNECT Response Framing Carefully

For an accepted WebTransport session:

- send `:status 200`
- do not close the CONNECT stream

With nghttp3, do not accidentally send FIN with the CONNECT acceptance. If using a data reader for an empty body, ensure the flags preserve the stream.

Failure signatures:

- immediate session close after acceptance usually means the response ended the stream
- long hang usually means the response was never sent

### 9. Validate SETTINGS And Capability Advertisement

If the browser reaches the HTTP/3 session but rejects WebTransport during opening handshake, inspect the emitted SETTINGS.

In this repo, check:

- `SETTINGS_ENABLE_CONNECT_PROTOCOL`
- `SETTINGS_H3_DATAGRAM`
- WebTransport-specific SETTINGS in vendored `third_party/nghttp3/lib/nghttp3_stream.c`

Important practice:

- do not trust a hardcoded server log line as proof the setting was emitted on the wire
- validate with Chromium netlog or an A/B test

When deciding whether a vendored nghttp3 patch is still needed:

1. remove only that patch
2. rebuild the focused server target
3. rerun the focused browser WebTransport test
4. compare the exact failure mode

If removing the patch regresses the browser test back to `Opening handshake failed`, keep the patch.

### 10. Confirm Child Stream Semantics

For bidirectional WebTransport streams, validate:

- stream-type prefix handling
- session-id association
- separation between the CONNECT control stream and child data streams
- write scheduling for raw child-stream bytes outside the normal HTTP response path

If the CONNECT succeeds but RPC bytes do not flow, inspect the child-stream parsing and write queues next.

### 11. Re-Run Focused Then Broader Validation

After each candidate fix:

1. rebuild the smallest relevant target
2. rerun the focused browser test
3. rerun adjacent HTTP/3 tests
4. only then run the broader JS or integration suite

Recommended sequence in this repo:

1. focused browser test in `test/js/test/webtransport-browser.test.ts`
2. HTTP/3 transport tests in JS integration suite
3. broader affected test targets if transport internals changed

## Decision Points

### If curl fails

- Fix listener, port, ALPN, or baseline HTTP/3 setup first.

### If curl works but page navigation fails in Chromium

- Suspect QUIC certificate trust first.
- Use a trusted localhost certificate for manual browser use.

### If WebTransport with `serverCertificateHashes` still fails

- Suspect capability advertisement, CONNECT semantics, or premature FIN.

### If browser reaches `/wt` but the server never handles CONNECT

- Inspect HTTP/3 request parsing callbacks and whether response startup incorrectly depends on `end_stream`.

### If CONNECT is accepted and the session closes immediately

- Inspect CONNECT response framing and whether the stream was ended accidentally.

### If only removing an nghttp3 patch breaks the browser test

- Keep the patch and document why it exists.

## Completion Checks

Do not consider the issue resolved until all of these are true:

- plain HTTP/3 works on the HTTP port
- browser trust problems are either fixed or intentionally bypassed for WebTransport with `serverCertificateHashes`
- Chromium reaches `/wt`
- CONNECT is accepted without hanging or immediate closure
- child streams can carry NPRPC framed traffic
- the focused browser smoke test passes
- related HTTP/3 tests still pass

## Common Failure Signatures

- `ERR_QUIC_PROTOCOL_ERROR`
  - often certificate trust or low-level QUIC rejection during browser navigation
- `WebTransportError: Opening handshake failed`
  - often missing SETTINGS, bad CONNECT semantics, or capability mismatch
- session opens then closes immediately
  - usually CONNECT response ended the stream
- server prints SETTINGS logs but never sees CONNECT
  - browser is still rejecting the session before request handling

## Useful Files

- `src/http/http3_server_nghttp3.cpp`
- `third_party/nghttp3/lib/nghttp3_stream.c`
- `test/js/test/webtransport-browser.test.ts`
- `test/src/common/helper.inl`
- `src/http/http_server.cpp`
- `include/nprpc/impl/nprpc_impl.hpp`
