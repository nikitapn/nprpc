// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT
//
// Injected into the page's MAIN world by content_script.ts.
// Installs window.__nprpc_debug which generated stubs (and the runtime)
// call to produce structured RPC trace events.
//
// API:
//   const id = window.__nprpc_debug.call_start(params)  → fires nprpc_call_start
//   window.__nprpc_debug.call_end(id, result)           → fires nprpc_call_end
//
// Both fire a CustomEvent('__nprpc_debug_event__') on window so the
// content-script can pick it up and forward it to the DevTools panel.

(function () {
  'use strict';

  if (globalThis.__nprpc_debug) return; // already installed

  let nextId = 1;
  const streamEntryIds = new Map();

  // Structured-clone (used by postMessage) rejects BigInt values.
  // Tag them as { __bigint__: "<value>" } so the DevTools panel can
  // reconstruct the correct type and display them as  101n.
  function sanitize(v) {
    if (typeof v === 'bigint') return { __bigint__: String(v) };
    if (Array.isArray(v)) return v.map(sanitize);
    if (v !== null && typeof v === 'object') {
      const out = {};
      for (const k of Object.keys(v)) out[k] = sanitize(v[k]);
      return out;
    }
    return v;
  }

  function dispatch(type, data) {
    window.dispatchEvent(
      new CustomEvent('__nprpc_debug_event__', { detail: { type, data: sanitize(data) } })
    );
  }

  globalThis.__nprpc_debug = {
    /**
     * Call from generated stub BEFORE marshalling.
     * @param {object} params - pre-marshalled call info
     * @returns {number} unique call id to pass to call_end
     */
    call_start(params) {
      const id = nextId++;
      dispatch('nprpc_call_start', {
        id,
        timestamp : Date.now(),
        status    : 'pending',
        ...params,
      });
      return id;
    },

    /**
     * Call from generated stub AFTER receiving the response (or on error).
     * @param {number} id - id returned by call_start
     * @param {object} result - { status, duration_ms, response_args?, response_bytes?, error? }
     */
    call_end(id, result) {
      dispatch('nprpc_call_end', { id, ...result });
    },

    stream_start(params) {
      const stream_id = String(params.stream_id);
      const id = nextId++;
      streamEntryIds.set(stream_id, id);
      dispatch('nprpc_stream_start', {
        id,
        timestamp: Date.now(),
        status: 'pending',
        ...params,
        stream_id,
      });
      return id;
    },

    stream_event(stream_id, event) {
      const key = String(stream_id);
      const id = streamEntryIds.get(key);
      if (id === undefined) return;

      dispatch('nprpc_stream_message', {
        id,
        stream_id: key,
        message: {
          timestamp: Date.now(),
          ...event,
        },
      });

      if (event.kind === 'complete' || event.kind === 'error' || event.kind === 'cancel') {
        streamEntryIds.delete(key);
      }
    },
  };
})();
