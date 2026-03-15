# @nprpc/devtools

Chrome DevTools extension for inspecting NPRPC traffic in real time. The panel shows unary RPC calls, stream lifecycle events, payload summaries, timings, byte counts, and transport metadata.

## Features

- Live request/response inspection for NPRPC calls.
- Stream session entries with nested chunk, completion, error, cancel, and window-update events.
- Sortable event list with duration, byte count, and status columns.

## Development

Install dependencies and build the extension bundle:

```sh
npm ci
npm run build
```

For local iteration:

```sh
npm run dev
```

## Loading In Chrome

1. Build the project.
2. Open `chrome://extensions`.
3. Enable Developer mode.
4. Choose Load unpacked.
5. Select the `dist/` directory from this package.

After loading the extension, open Chrome DevTools on a page that uses NPRPC and switch to the NPRPC panel.

## Packaging

`npm pack` publishes the built extension bundle and README. The package includes `dist/` so consumers can unpack the already-built extension without running Vite themselves.

The extension manifest version is synchronized from `package.json` before each build, so the npm package version is the only release version to update.

`npm run prepack` rebuilds the extension before packaging.
