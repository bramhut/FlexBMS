import { fileURLToPath, URL } from 'node:url'
import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig(({ mode }) => ({
  base: './',
  define: {
    __FLEXBMS_TARGET__: JSON.stringify(mode === 'gateway' ? 'gateway' : 'webserial'),
  },
  plugins: [vue()],
  resolve: { alias: { '@': fileURLToPath(new URL('./src', import.meta.url)) } },
}))
