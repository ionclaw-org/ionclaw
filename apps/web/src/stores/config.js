import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useApi } from '../composables/useApi'

export const useConfigStore = defineStore('config', () => {
  const api = useApi()
  const config = ref(null)

  async function loadConfig() {
    try {
      config.value = await api.get('/config')
    } catch (e) {
      console.error('[config] load error:', e)
    }
  }

  async function saveSection(section, data) {
    await api.put(`/config/${section}`, { data })
    await loadConfig()
  }

  return { config, loadConfig, saveSection }
})
