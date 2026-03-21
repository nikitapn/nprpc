import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import { resolve } from 'path'

// https://vite.dev/config/
export default defineConfig({
  plugins: [svelte()],
  build: {
    rollupOptions: {
      input: {
        panel: 'src/panel.html',
        content_script: resolve(__dirname, 'src/content_script.ts'),
      },
      output: {
        // content_script must keep a predictable flat filename so the
        // extension manifest can reference it without a content hash.
        entryFileNames: (chunk) =>
          chunk.name === 'content_script'
            ? '[name].js'
            : 'assets/[name]-[hash].js',
      },
    },
  },
})
