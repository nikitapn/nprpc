// @nprpc/adapter-sveltekit
// SvelteKit adapter that serves SSR via shared memory IPC with NPRPC C++ server

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const files = fileURLToPath(new URL('./files', import.meta.url));

/**
 * @typedef {Object} AdapterOptions
 * @property {string} [out] - Build output directory (default: 'build')
 * @property {string} [channelId] - Shared memory channel ID (default: auto-generated)
 * @property {boolean} [precompress] - Whether to precompress static assets (default: true)
 */

/** @type {import('./index.js').default} */
export default function adapter(opts = {}) {
    const { 
        out = 'build', 
        channelId,
        precompress = true 
    } = opts;

    return {
        name: '@nprpc/adapter-sveltekit',

        /** @param {import('@sveltejs/kit').Builder} builder */
        async adapt(builder) {
            const tmp = builder.getBuildDirectory('adapter-nprpc');

            builder.rimraf(out);
            builder.rimraf(tmp);
            builder.mkdirp(tmp);
            builder.mkdirp(`${out}/server`);

            builder.log.minor('Copying assets');
            builder.writeClient(`${out}/client${builder.config.kit.paths.base}`);
            builder.writePrerendered(`${out}/prerendered${builder.config.kit.paths.base}`);

            if (precompress) {
                builder.log.minor('Compressing assets');
                await Promise.all([
                    builder.compress(`${out}/client`),
                    builder.compress(`${out}/prerendered`)
                ]);
            }

            builder.log.minor('Building server');
            builder.writeServer(tmp);

            // Generate manifest
            writeFileSync(
                `${tmp}/manifest.js`,
                [
                    `export const manifest = ${builder.generateManifest({ relativePath: './' })};`,
                    `export const prerendered = new Set(${JSON.stringify(builder.prerendered.paths)});`,
                    `export const base = ${JSON.stringify(builder.config.kit.paths.base)};`
                ].join('\n\n')
            );

            // Copy server files to output
            builder.copy(tmp, `${out}/server`);

            // Copy handler files
            builder.copy(files, out, {
                replace: {
                    MANIFEST: './server/manifest.js',
                    SERVER: './server/index.js',
                    CHANNEL_ID: channelId ? JSON.stringify(channelId) : 'null'
                }
            });

            builder.log.minor('Build complete');
            builder.log.info(`Output: ${out}`);
            builder.log.info('Start with: node build/index.js');
        },

        supports: {
            read: () => true
        }
    };
}
