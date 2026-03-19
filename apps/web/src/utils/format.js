export function humanizeToolName(name) {
  if (!name || typeof name !== 'string') return name || ''
  return name
    .replace(/_/g, ' ')
    .replace(/\b\w/g, (c) => c.toUpperCase())
}
