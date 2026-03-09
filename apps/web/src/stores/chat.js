import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useApi } from '../composables/useApi'

/**
 * Extract the chat_id from a session key.
 * "web:abc-123" → "abc-123", "heartbeat:heartbeat" → "heartbeat"
 */
function chatIdFromKey(key) {
  const idx = key.indexOf(':')
  return idx !== -1 ? key.slice(idx + 1) : key
}

/**
 * Extract the channel from a session key.
 * "web:abc-123" → "web", "heartbeat:heartbeat" → "heartbeat"
 */
function channelFromKey(key) {
  const idx = key.indexOf(':')
  return idx !== -1 ? key.slice(0, idx) : key
}

/**
 * Display label for a session key.
 */
function sessionLabel(key) {
  const channel = channelFromKey(key)
  const chatId = chatIdFromKey(key)

  if (channel === 'heartbeat') return 'Heartbeat'
  if (channel === 'cron') return `Cron ${chatId}`
  if (channel === 'telegram') return `Telegram ${chatId}`
  if (channel === 'web') {
    return chatId.length > 12 ? `Chat ${chatId.slice(0, 8)}` : chatId
  }
  return key
}

export { chatIdFromKey, channelFromKey, sessionLabel }

export const useChatStore = defineStore('chat', () => {
  const api = useApi()
  const messages = ref([])
  const sessions = ref([])
  let stored = localStorage.getItem('chat_session')
  // migrate bare UUIDs from older versions to full session keys
  if (stored && !stored.includes(':')) {
    stored = `web:${stored}`
    localStorage.setItem('chat_session', stored)
  }
  const currentSessionId = ref(stored || `web:${crypto.randomUUID()}`)
  if (!stored) localStorage.setItem('chat_session', currentSessionId.value)

  const liveMessage = ref(null)
  const toolRunning = ref(false)

  // per-chatId accumulator for streamed content (survives session switches)
  const _liveCache = new Map()

  function _chatId(data) {
    const raw = data?.chat_id || currentSessionId.value
    return chatIdFromKey(raw)
  }

  function _isCurrent(chatId) {
    return chatId === chatIdFromKey(currentSessionId.value)
  }

  function _ensureLive(data) {
    const id = _chatId(data)
    let state = _liveCache.get(id)
    if (!state) {
      state = { content: [], agent_name: data?.agent_name || '', toolRunning: false }
      _liveCache.set(id, state)
    } else if (data?.agent_name && !state.agent_name) {
      state.agent_name = data.agent_name
    }
    return state
  }

  // deep-copy blocks so Vue v-for detects nested text mutations
  function _syncLive(chatId) {
    if (!_isCurrent(chatId)) return
    const state = _liveCache.get(chatId)
    if (state) {
      liveMessage.value = {
        role: 'assistant',
        content: state.content.map(b => ({ ...b })),
        agent_name: state.agent_name,
      }
      toolRunning.value = state.toolRunning
    }
  }

  function _clearLive(chatId) {
    _liveCache.delete(chatId)
    if (_isCurrent(chatId)) {
      liveMessage.value = null
      toolRunning.value = false
    }
  }

  function _lastTextBlock(state) {
    const last = state.content[state.content.length - 1]
    if (last?.type === 'text') return last
    const block = { type: 'text', text: '' }
    state.content.push(block)
    return block
  }

  // ------------------------------------------------------------------
  // send message
  // ------------------------------------------------------------------

  async function sendMessage(text, media = []) {
    const userMsg = { role: 'user', content: text, media, timestamp: Date.now() }
    messages.value.push(userMsg)
    const sessionKey = currentSessionId.value
    const chatId = chatIdFromKey(sessionKey)
    _ensureLive({ chat_id: chatId })
    _syncLive(chatId)

    try {
      const res = await api.post('/chat', {
        message: text,
        session_id: sessionKey,
        media,
      })
      return res
    } catch (e) {
      const idx = messages.value.indexOf(userMsg)
      if (idx !== -1) messages.value.splice(idx, 1)
      _clearLive(chatId)
      throw e
    }
  }

  // ------------------------------------------------------------------
  // websocket event handlers
  // ------------------------------------------------------------------

  function onUserMessage(data) {
    const chatId = _chatId(data)
    _ensureLive(data)
    if (!_isCurrent(chatId)) return
    messages.value.push({
      role: 'user',
      content: data.content,
      media: data.media,
      channel: data.channel,
      timestamp: Date.now(),
    })
    _syncLive(chatId)
  }

  function onMessage(data) {
    const chatId = _chatId(data)

    _liveCache.delete(chatId)

    if (!_isCurrent(chatId)) {
      // response arrived for a session the user navigated away from;
      // refresh sessions so the sidebar reflects the update
      loadSessions()
      return
    }

    liveMessage.value = null
    toolRunning.value = false

    // deduplicate: the same task_id may arrive more than once
    if (data.task_id) {
      const exists = messages.value.some(
        (m) => m.role === 'assistant' && m.task_id === data.task_id
      )
      if (exists) return
    }

    // normalize content to block array
    const content = Array.isArray(data.content)
      ? data.content
      : [{ type: 'text', text: String(data.content || '') }]

    messages.value.push({
      role: 'assistant',
      content,
      task_id: data.task_id,
      agent_name: data.agent_name,
      timestamp: Date.now(),
    })
  }

  function onThinking(data) {
    const chatId = _chatId(data)
    _ensureLive(data)
    _syncLive(chatId)
  }

  function onTranscription(data) {
    const chatId = _chatId(data)
    if (!_isCurrent(chatId)) return
    for (let i = messages.value.length - 1; i >= 0; i--) {
      if (messages.value[i].role === 'user') {
        const msg = messages.value[i]
        msg.content = msg.content
          ? `${msg.content}\n\n${data.content}`
          : data.content
        break
      }
    }
  }

  function onWarning(data) {
    const chatId = _chatId(data)
    if (!_isCurrent(chatId)) return
    messages.value.push({
      role: 'system',
      content: data.content,
      agent_name: data.agent_name,
      timestamp: Date.now(),
    })
  }

  function onTyping(data) {
    const chatId = _chatId(data)
    const state = _ensureLive(data)
    state.toolRunning = false
    _syncLive(chatId)
  }

  function onStream(data) {
    const chatId = _chatId(data)
    const state = _ensureLive(data)
    const block = _lastTextBlock(state)
    block.text += data.content || ''
    state.toolRunning = false
    _syncLive(chatId)
  }

  function onStreamEnd(data) {
    const chatId = _chatId(data)
    if (!_isCurrent(chatId)) return
    toolRunning.value = false
  }

  function onToolUse(data) {
    const chatId = _chatId(data)
    const state = _ensureLive(data)
    state.content.push({
      type: 'tool_use',
      name: data.tool,
      description: data.description,
    })
    state.toolRunning = true
    _syncLive(chatId)
  }

  // ------------------------------------------------------------------
  // session management
  // ------------------------------------------------------------------

  async function loadSessions() {
    try {
      sessions.value = await api.get('/chat/sessions')
    } catch (e) {
      console.error('[chat] loadSessions error:', e)
    }
  }

  // key of the session currently being loaded (prevents duplicate concurrent fetches)
  let _loadingKey = null

  async function loadHistory(sessionKey) {
    if (!sessionKey) return

    const switching = currentSessionId.value !== sessionKey

    currentSessionId.value = sessionKey
    localStorage.setItem('chat_session', sessionKey)

    // clear display immediately when switching so the previous session's
    // stale messages / live indicators are never visible on the new session
    if (switching) {
      messages.value = []
      liveMessage.value = null
      toolRunning.value = false
    }

    // skip the API call if this exact session is already being fetched;
    // currentSessionId was already updated above so the UI reflects the switch
    if (_loadingKey === sessionKey) return

    _loadingKey = sessionKey

    try {
      const data = await api.get(`/chat/sessions/${sessionKey}`)

      // discard if the user switched to another session while the call was in flight
      if (currentSessionId.value !== sessionKey) return

      messages.value = (data.messages || []).map((m) => ({
        ...m,
        timestamp: m.timestamp ? new Date(m.timestamp).getTime() : Date.now(),
      }))

      const chatId = chatIdFromKey(sessionKey)
      const backendLive = data.live_state?.tool_uses?.length > 0

      if (backendLive) {
        // prefer client-side cache (has accumulated stream content)
        const cached = _liveCache.get(chatId)
        if (cached) {
          liveMessage.value = {
            role: 'assistant',
            content: cached.content.map(b => ({ ...b })),
            agent_name: cached.agent_name,
          }
          toolRunning.value = cached.toolRunning
        } else {
          liveMessage.value = {
            role: 'assistant',
            content: data.live_state.tool_uses.map((tu) => ({
              type: 'tool_use',
              name: tu.name,
              description: tu.description,
            })),
            agent_name: data.live_state?.agent_name || '',
          }
          toolRunning.value = true
        }
      } else {
        // restore from client-side cache if the agent is still streaming
        // and we accumulated events while viewing another session
        const cached = _liveCache.get(chatId)
        if (cached && cached.content.length > 0) {
          liveMessage.value = {
            role: 'assistant',
            content: cached.content.map(b => ({ ...b })),
            agent_name: cached.agent_name,
          }
          toolRunning.value = cached.toolRunning
        } else {
          _liveCache.delete(chatId)
          liveMessage.value = null
          toolRunning.value = false
        }
      }
    } catch (e) {
      if (currentSessionId.value !== sessionKey) return
      console.error('[chat] loadHistory error:', e)
      messages.value = []
      liveMessage.value = null
      toolRunning.value = false
    } finally {
      if (_loadingKey === sessionKey) _loadingKey = null
    }
  }

  async function switchSession(sessionKey) {
    await loadHistory(sessionKey)
    loadSessions()
  }

  async function clearSession(sessionKey) {
    await api.del(`/chat/sessions/${sessionKey}`)
    _liveCache.delete(chatIdFromKey(sessionKey))
    if (sessionKey === currentSessionId.value) {
      messages.value = []
      liveMessage.value = null
      toolRunning.value = false
    }
    await loadSessions()
  }

  function onSessionsUpdated() {
    loadSessions()
  }

  function newSession() {
    const key = `web:${crypto.randomUUID()}`
    currentSessionId.value = key
    localStorage.setItem('chat_session', key)
    messages.value = []
    liveMessage.value = null
    toolRunning.value = false
  }

  return {
    messages,
    sessions,
    currentSessionId,
    liveMessage,
    toolRunning,
    sendMessage,
    onUserMessage,
    onMessage,
    onThinking,
    onTyping,
    onStream,
    onStreamEnd,
    onToolUse,
    onTranscription,
    onWarning,
    onSessionsUpdated,
    loadSessions,
    loadHistory,
    switchSession,
    clearSession,
    newSession,
  }
})
