import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import router from '../router'
import * as storage from '../utils/storage'

export const useAuthStore = defineStore('auth', () => {
  const token = ref(storage.getItem('ionclaw_token') || '')
  const username = ref(storage.getItem('ionclaw_username') || '')
  const isAuthenticated = computed(() => !!token.value)

  async function login(user, password) {
    const res = await fetch('/api/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ username: user, password }),
    })

    if (!res.ok) {
      let message = 'Login failed'
      try {
        const data = await res.json()
        if (data.error) message = data.error
      } catch {
        message = await res.text().catch(() => 'Login failed')
      }
      throw new Error(message)
    }

    const data = await res.json()
    token.value = data.token
    username.value = data.username
    storage.setItem('ionclaw_token', data.token)
    storage.setItem('ionclaw_username', data.username)
  }

  function logout() {
    token.value = ''
    username.value = ''
    storage.removeItem('ionclaw_token')
    storage.removeItem('ionclaw_username')
    router.push('/login')
  }

  return { token, username, isAuthenticated, login, logout }
})
