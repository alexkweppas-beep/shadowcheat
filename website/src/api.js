// Централизованный API URL — читается из .env.production / .env.development
// В production: https://shadowcheats.cc
// В dev: https://shadowcheats.cc
export const API_BASE = import.meta.env.VITE_API_URL || 'https://shadowcheats.cc'

export const apiFetch = (path, options = {}) =>
  fetch(`${API_BASE}${path}`, options)
