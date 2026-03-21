import { useAuthStore } from '../stores/auth'

const BASE_URL = '/api'

export function useApi() {
  function getHeaders(extra = {}) {
    const auth = useAuthStore()
    const headers = { ...extra }
    if (auth.token) {
      headers['Authorization'] = `Bearer ${auth.token}`
    }
    return headers
  }

  async function handleError(res) {
    if (res.status === 401) {
      const auth = useAuthStore()
      auth.logout()
      throw new Error('Session expired')
    }

    if (!res.ok) {
      const text = await res.text()
      let message = text
      try {
        const json = JSON.parse(text)
        if (json.error) message = json.error
      } catch (e) {
        console.warn('[api] Failed to parse error response as JSON:', e.message)
      }
      throw new Error(message)
    }
  }

  async function request(path, options = {}) {
    const url = `${BASE_URL}${path}`
    const res = await fetch(url, {
      ...options,
      headers: getHeaders({ 'Content-Type': 'application/json', ...options.headers }),
    })

    await handleError(res)

    const contentType = res.headers.get('content-type') || ''
    if (contentType.includes('application/json')) {
      return res.json()
    }

    // non-json response: return raw text or empty object for 204/no-content
    const text = await res.text()
    if (!text) return {}
    return { text }
  }

  function get(path) {
    return request(path)
  }

  function post(path, body) {
    return request(path, { method: 'POST', body: JSON.stringify(body) })
  }

  function put(path, body) {
    return request(path, { method: 'PUT', body: JSON.stringify(body) })
  }

  function del(path) {
    return request(path, { method: 'DELETE' })
  }

  async function upload(path, files) {
    const form = new FormData()
    files.forEach((f, i) => form.append(`file_${i}`, f))
    const url = `${BASE_URL}${path}`
    const res = await fetch(url, {
      method: 'POST',
      headers: getHeaders(),
      body: form,
    })

    await handleError(res)
    return res.json()
  }

  async function downloadFile(path, filename) {
    const clean = path.startsWith('/') ? path.slice(1) : path
    const url = `${BASE_URL}/files/download/${clean}`
    const res = await fetch(url, { headers: getHeaders() })

    await handleError(res)

    const blob = await res.blob()
    const blobUrl = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = blobUrl
    a.download = filename || clean.split('/').pop()
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(blobUrl)
  }

  return { get, post, put, del, upload, downloadFile }
}
