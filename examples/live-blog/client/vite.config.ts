import { defineConfig } from 'vite';

// Builds the browser islands only — chat and video.
//
// Pages are rendered by the backend from Mustache templates, so there is no
// router, no SSR and no framework here: just the two components that need a
// live NPRPC connection, emitted as one module into the static root.
export default defineConfig({
	build: {
		outDir: 'build/client',
		// The static root also holds app.css, vendor/ and the server-generated
		// host.json. Assembly and cleanup belong to scripts/build-assets.sh.
		emptyOutDir: false,
		target: 'es2022',
		sourcemap: true,
		rollupOptions: {
			input: 'src/islands/main.ts',
			output: {
				entryFileNames: 'islands.js',
				chunkFileNames: 'islands-[hash].js',
				assetFileNames: 'islands-[name][extname]'
			}
		}
	},
	server: {
		host: '0.0.0.0',
		allowedHosts: ['localhost', '127.0.0.1'],
		proxy: {
			'/host.json': {
				target: 'https://localhost:8443',
				changeOrigin: true,
				secure: false
			}
		}
	}
});
