import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { resolve } from 'path'

// JavaScript Obfuscator для production builds
// npm install --save-dev javascript-obfuscator vite-plugin-javascript-obfuscator
let obfuscatorPlugin = null
try {
  const { obfuscator } = await import('vite-plugin-javascript-obfuscator')
  obfuscatorPlugin = obfuscator({
    apply: 'build', // Только в production build
    options: {
      // ── Базовые настройки ──
      compact: true,
      simplify: true,
      selfDefending: true,          // Ломает себя если сжать/отформатировать
      
      // ── Переименование ──
      identifierNamesGenerator: 'hexadecimal',
      renameGlobals: false,         // Не переименовывать глобальные (сломает React)
      
      // ── Control Flow Flattening ──
      controlFlowFlattening: true,
      controlFlowFlatteningThreshold: 0.75,
      
      // ── Строки ──
      stringArray: true,
      stringArrayEncoding: ['rc4'],  // RC4 шифрование строк
      stringArrayThreshold: 0.85,
      stringArrayWrappersCount: 5,
      stringArrayWrappersType: 'function',
      stringArrayRotate: true,
      stringArrayShuffle: true,
      
      // ── Bogus Code ──
      deadCodeInjection: true,
      deadCodeInjectionThreshold: 0.4,
      
      // ── Числа ──
      numbersToExpressions: true,
      
      // ── Трансформации ──
      transformObjectKeys: true,
      unicodeEscapeSequence: false, // Не используем unicode (тормозит)
      
      // ── Target ──
      target: 'browser',
      
      // ── Защита от отладки в браузере ──
      debugProtection: true,
      debugProtectionInterval: 2000, // Ломает DevTools каждые 2 сек
      disableConsoleOutput: true,    // Отключить console.log в prod
      domainLock: ['shadowcheats.cc'],               // TODO: добавить ['shadowcheats.cc'] перед релизом
      
      // ── Seed (детерминированный для одного билда) ──
      seed: Date.now(),
    }
  })
} catch {
  // vite-plugin-javascript-obfuscator не установлен — игнорируем
  console.warn('[shadow] JS Obfuscator not found — skipping. Run: npm install --save-dev javascript-obfuscator vite-plugin-javascript-obfuscator')
}

export default defineConfig({
  plugins: [
    react(),
    ...(obfuscatorPlugin ? [obfuscatorPlugin] : []),
  ],

  // ── Сборка ──
  build: {
    outDir: 'dist',
    sourcemap: false,         // НЕТ source maps в production!
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,   // Убрать console.log
        drop_debugger: true,  // Убрать debugger statements
        pure_funcs: ['console.log', 'console.info', 'console.debug', 'console.warn'],
        passes: 3,
      },
      mangle: {
        toplevel: true,
      },
      format: {
        comments: false,      // Убрать все комментарии
      },
    },
    rollupOptions: {
      output: {
        // Рандомизировать имена chunk-файлов (не 'index.js')
        entryFileNames:   `assets/[hash].js`,
        chunkFileNames:   `assets/[hash].js`,
        assetFileNames:   `assets/[hash].[ext]`
      },
    },
  },

  // ── Dev сервер ──
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'https://shadowcheats.cc',
        changeOrigin: true,
      },
    },
  },

  // ── Переменные окружения ──
  define: {
    // Убрать development-only код в production
    __DEV__: JSON.stringify(process.env.NODE_ENV === 'development'),
  },

  resolve: {
    alias: {
      '@': resolve(__dirname, 'src'),
    },
  },
})
