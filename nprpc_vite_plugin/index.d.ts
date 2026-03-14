import type { Plugin } from 'vite';

export interface NprpcDevReloadOptions {
  /**
   * Base URL of the NPRPC C++ backend.
   * @default 'https://localhost:8443'
   */
  backendUrl?: string;

  /**
   * Path of the reload endpoint exposed by the backend.
   * @default '/_nprpc/dev/reload'
   */
  reloadPath?: string;
}

/**
 * Vite plugin that notifies the NPRPC C++ backend after each successful build
 * (in watch mode only) so it can restart its SSR Node.js worker and
 * regenerate host.json.
 *
 * @example
 * // vite.config.ts
 * import { nprpcDevReload } from '@nprpc/vite-plugin';
 *
 * export default defineConfig({
 *   plugins: [sveltekit(), nprpcDevReload()],
 * });
 */
export declare function nprpcDevReload(options?: NprpcDevReloadOptions): Plugin;

export default nprpcDevReload;
