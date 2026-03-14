
import adapter from '@nprpc/adapter-sveltekit';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	kit: {
		adapter: adapter({
			out: 'build',
			channelId: 'svelte-ssr-demo'
		})
	}
};

export default config;