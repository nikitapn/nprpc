import tailwindcss from '@tailwindcss/vite';
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

const nprpcShim = new URL('./src/my_modules/nprpc.ts', import.meta.url).pathname;

export default defineConfig({
	resolve: {
		alias: {
			nprpc: nprpcShim
		}
	},
	plugins: [tailwindcss(), sveltekit()],
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