/**
 * Safe localStorage wrapper — handles private browsing, disabled storage,
 * and other environments where localStorage throws.
 */
const memoryFallback = new Map()

export function getItem(key) {
  try {
    return localStorage.getItem(key)
  } catch {
    return memoryFallback.get(key) ?? null
  }
}

export function setItem(key, value) {
  try {
    localStorage.setItem(key, value)
  } catch {
    memoryFallback.set(key, value)
  }
}

export function removeItem(key) {
  try {
    localStorage.removeItem(key)
  } catch {
    memoryFallback.delete(key)
  }
}
