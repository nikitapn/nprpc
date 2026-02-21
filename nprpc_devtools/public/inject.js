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

  function dispatch(type, data) {
    window.dispatchEvent(
      new CustomEvent('__nprpc_debug_event__', { detail: { type, data } })
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
  };
})();
