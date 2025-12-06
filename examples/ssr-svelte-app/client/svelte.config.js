import adapter from '@nprpc/adapter-sveltekit';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	// Consult https://svelte.dev/docs/kit/integrations
	// for more information about preprocessors
	preprocess: vitePreprocess(),

	kit: {
		// Use NPRPC adapter for SSR via shared memory
		adapter: adapter({
			out: 'build',
			channelId: 'svelte-ssr-demo'
		})
	}
};

export default config;
