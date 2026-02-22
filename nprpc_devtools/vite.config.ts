import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import { resolve } from 'path'

// https://vite.dev/config/
export default defineConfig({
  plugins: [svelte()],
  build: {
    rollupOptions: {
      input: {
        panel         : 'src/panel.html',
        background    : resolve(__dirname, 'src/background.ts'),
        content_script: resolve(__dirname, 'src/content_script.ts'),
      },
      output: {
        // background and content_script must be predictable flat filenames
        // so manifest.json can reference them without content-hash.
        entryFileNames: (chunk) =>
          ['background', 'content_script'].includes(chunk.name)
            ? '[name].js'
            : 'assets/[name]-[hash].js',
      },
    },
  },
})
