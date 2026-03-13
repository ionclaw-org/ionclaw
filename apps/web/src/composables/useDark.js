import { ref } from 'vue'
import * as storage from '../utils/storage'

function getInitialDark() {
  const saved = storage.getItem('dark_mode')
  if (saved !== null) return saved === 'true'
  return window.matchMedia('(prefers-color-scheme: dark)').matches
}

const isDark = ref(getInitialDark())
document.documentElement.classList.toggle('dark-mode', isDark.value)

export function useDark() {
  return isDark
}

export function toggleDark() {
  isDark.value = !isDark.value
  storage.setItem('dark_mode', isDark.value)
  document.documentElement.classList.toggle('dark-mode', isDark.value)
}
