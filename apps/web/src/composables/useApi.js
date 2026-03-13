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
      } catch {}
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
    return res.json()
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

  function patch(path, body) {
    return request(path, { method: 'PATCH', body: JSON.stringify(body) })
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

  async function downloadFile(path) {
    const clean = path.startsWith('/') ? path.slice(1) : path
    const url = `${BASE_URL}/files/download/${clean}`
    const res = await fetch(url, { headers: getHeaders() })

    await handleError(res)

    const blob = await res.blob()
    const blobUrl = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = blobUrl
    a.download = clean.split('/').pop()
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(blobUrl)
  }

  return { get, post, put, patch, del, upload, downloadFile }
}
