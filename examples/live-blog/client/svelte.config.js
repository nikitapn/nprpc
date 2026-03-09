
import adapter from '@nprpc/adapter-sveltekit';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

const nprpcShim = new URL('./src/my_modules/nprpc.ts', import.meta.url).pathname;

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	kit: {
    // Use NPRPC adapter for SSR via shared memory
		adapter: adapter({
			out: 'build',
			channelId: 'svelte-ssr-demo'
		}),
		alias: {
			nprpc: nprpcShim
		}
	}
};

export default config;