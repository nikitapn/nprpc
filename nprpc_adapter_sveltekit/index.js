// @nprpc/adapter-sveltekit
// SvelteKit adapter that serves SSR via shared memory IPC with NPRPC C++ server

import { cpSync, existsSync, mkdirSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { rollup } from 'rollup';
import { nodeResolve } from '@rollup/plugin-node-resolve';
import commonjs from '@rollup/plugin-commonjs';
import json from '@rollup/plugin-json';

const files = fileURLToPath(new URL('./files', import.meta.url));
const require = createRequire(import.meta.url);

function copyIfExists(from, to, dereference = false) {
    if (!existsSync(from)) {
        return;
    }

    const resolvedFrom = existsSync(from) ? realpathSync(from) : path.resolve(from);
    const resolvedTo = existsSync(to) ? realpathSync(to) : path.resolve(to);
    if (resolvedFrom === resolvedTo) {
        return;
    }

    cpSync(from, to, { recursive: true, dereference });
}

function stageNprpcNodeRuntime(out) {
    const packageJsonPath = require.resolve('@nprpc/node_addon/package.json');
    const packageDir = path.dirname(packageJsonPath);
    const runtimeDir = path.join(out, 'node_modules', '@nprpc', 'node_addon');

    rmSync(runtimeDir, { recursive: true, force: true });
    mkdirSync(runtimeDir, { recursive: true });

    // Copy only the essential runtime files — no sources, makefiles, tests, or
    // object files.  The native addon is placed at ./nprpc_shm.node (the first
    // candidate in index.js) so no build/ sub-directory is needed at all.
    for (const f of ['index.js', 'package.json']) {
        const src = path.join(packageDir, f);
        if (existsSync(src)) cpSync(src, path.join(runtimeDir, f));
    }

    // Locate the compiled .node binary (Release preferred, Debug fallback).
    for (const variant of ['Release', 'Debug']) {
        const src = path.join(packageDir, 'build', variant, 'nprpc_shm.node');
        if (existsSync(src)) {
            cpSync(src, path.join(runtimeDir, 'nprpc_shm.node'));
            break;
        }
    }
}

/**
 * Copy a single package directory into destNodeModules, preserving scoped
 * package paths (e.g. @scope/pkg → node_modules/@scope/pkg).
 *
 * @param {string} packageName
 * @param {string} destNodeModules
 */
function stagePackage(packageName, destNodeModules) {
    let packageDir;
    try {
        packageDir = path.dirname(require.resolve(`${packageName}/package.json`));
    } catch {
        // package has a restrictive exports field — resolve main and walk up
        try {
            let file = require.resolve(packageName);
            let dir = path.dirname(file);
            while (true) {
                if (existsSync(path.join(dir, 'package.json'))) { packageDir = dir; break; }
                const parent = path.dirname(dir);
                if (parent === dir) break;
                dir = parent;
            }
        } catch {}
    }
    if (!packageDir) return;

    const parts = packageName.startsWith('@') ? packageName.split('/') : [packageName];
    const destDir = path.join(destNodeModules, ...parts);
    if (!existsSync(destDir)) {
        mkdirSync(path.dirname(destDir), { recursive: true });
        cpSync(packageDir, destDir, { recursive: true, dereference: true });
    }
}

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

            // Re-bundle the Vite server output with Rollup, inlining all
            // framework code (svelte, @sveltejs/kit, clsx, etc.) so that only
            // the project's own `dependencies` need to exist at runtime.
            // This mirrors how @sveltejs/adapter-node works.
            builder.log.minor('Bundling server (Rollup)');
            const pkg = JSON.parse(readFileSync('package.json', 'utf8'));
            const bundle = await rollup({
                input: {
                    index: `${tmp}/index.js`,
                    manifest: `${tmp}/manifest.js`
                },
                external: [
                    // Keep project dependencies external so they are loaded
                    // from build/node_modules at runtime.
                    ...Object.keys(pkg.dependencies ?? {}).map((d) => new RegExp(`^${d}(\/.*)?$`)),
                    // Always keep the native addon external.
                    /^@nprpc\/node_addon(\/.*)?$/
                ],
                plugins: [
                    nodeResolve({ preferBuiltins: true, exportConditions: ['node'] }),
                    commonjs({ strictRequires: true }),
                    json()
                ]
            });
            await bundle.write({
                dir: `${out}/server`,
                format: 'esm',
                sourcemap: true,
                chunkFileNames: 'chunks/[name]-[hash].js'
            });
            await bundle.close();

            // Copy handler files
            builder.copy(files, out, {
                replace: {
                    MANIFEST: './server/manifest.js',
                    SERVER: './server/index.js',
                    CHANNEL_ID: channelId ? JSON.stringify(channelId) : 'null'
                }
            });

            writeFileSync(
                `${out}/package.json`,
                JSON.stringify({ type: 'module', private: true }, null, 2)
            );

            builder.log.minor('Staging @nprpc/node_addon native runtime');
            stageNprpcNodeRuntime(out);

            builder.log.minor('Build complete');
            builder.log.info(`Output: ${out}`);
            builder.log.info('Start with: node build/index.js');
        },

        supports: {
            read: () => true
        }
    };
}
