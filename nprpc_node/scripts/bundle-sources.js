// Copies the nprpc C++ sources and headers that binding.gyp references via
// relative `../` paths into self-contained subdirectories so the package can
// be compiled after `npm install` without the full nprpc monorepo present.
//
// Run automatically via `prepack` before `npm publish`.

import { cpSync, mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const pkgRoot  = resolve(__dirname, '..');
const nprpcRoot = resolve(pkgRoot, '..');

const copy = (src, dst) => {
  mkdirSync(resolve(pkgRoot, dirname(dst)), { recursive: true });
  cpSync(resolve(nprpcRoot, src), resolve(pkgRoot, dst), { recursive: true });
  console.log(`  ${src} → ${dst}`);
};

console.log('Bundling nprpc sources into package...');
copy('src/shm',           'nprpc_src/shm');
copy('src/flat_buffer.cpp', 'nprpc_src/flat_buffer.cpp');
copy('src/logging.cpp',    'nprpc_src/logging.cpp');
copy('src/logging.hpp',    'nprpc_src/logging.hpp');
copy('src/debug.hpp',      'nprpc_src/debug.hpp');
copy('include',            'nprpc_include');
console.log('Done.');
