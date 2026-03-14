// @nprpc/vite-plugin
// Vite plugin that notifies the NPRPC C++ backend after each successful build
// so it can restart its SSR Node.js worker and regenerate host.json.
//
// The backend must be built with NPRPC_SSR_ENABLED and started with
// watch_files() enabled; it exposes POST /_nprpc/dev/reload for this purpose.
//
// The notification fires only in watch mode (npm run dev / vite build --watch)
// so a plain production `npm run build` is never affected.

import https from 'node:https';
import http from 'node:http';

/**
 * @param {object} [options]
 * @param {string} [options.backendUrl] Base URL of the NPRPC backend.
 *   Defaults to 'https://localhost:8443'.
 * @param {string} [options.reloadPath] Path of the reload endpoint.
 *   Defaults to '/_nprpc/dev/reload'.
 * @returns {import('vite').Plugin}
 */
export function nprpcDevReload(options = {}) {
  const backendUrl = options.backendUrl ?? 'https://localhost:8443';
  const reloadPath = options.reloadPath ?? '/_nprpc/dev/reload';

  return {
    name: 'nprpc-dev-reload',

    writeBundle() {
      if (!this.meta.watchMode) return;

      const url = new URL(reloadPath, backendUrl);
      const transport = url.protocol === 'https:' ? https : http;

      const req = transport.request(
        {
          hostname: url.hostname,
          port: url.port || (url.protocol === 'https:' ? 443 : 80),
          path: url.pathname,
          method: 'POST',
          // Self-signed certificates are common in local dev.
          rejectUnauthorized: false
        },
        (res) => { res.resume(); }
      );

      req.on('error', () => {
        // Silently ignore — the backend may not be running yet on first build.
      });

      req.end();
    }
  };
}

export default nprpcDevReload;
