import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useApi } from '../composables/useApi'

export const useAgentsStore = defineStore('agents', () => {
  const api = useApi()
  const agents = ref([])

  async function loadAgents() {
    try {
      agents.value = await api.get('/agents')
    } catch (e) {
      console.error('[agents] load error:', e)
    }
  }

  return { agents, loadAgents }
})
