import { defineStore } from 'pinia'
import { ref } from 'vue'
import { v4 as uuidv4 } from 'uuid'
import { useApi } from '../composables/useApi'
import * as storage from '../utils/storage'

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
 * Display label for a session key, optionally using a server-provided display name.
 */
function sessionLabel(key, displayName) {
  // prefer server-provided display name (auto-generated from first message)
  if (displayName && displayName !== key) {
    return displayName
  }

  const channel = channelFromKey(key)
  const chatId = chatIdFromKey(key)

  if (channel === 'heartbeat') return 'Heartbeat'
  if (channel === 'cron') return `Cron ${chatId}`
  if (channel === 'telegram') return `Telegram ${chatId}`
  if (channel === 'mcp') return `MCP ${chatId.slice(0, 8)}`
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
  const DEFAULT_SESSION = 'web:main'
  const stored = storage.getItem('chat_session')
  const currentSessionId = ref(stored || DEFAULT_SESSION)
  if (!stored) storage.setItem('chat_session', currentSessionId.value)

  const liveMessage = ref(null)
  const toolRunning = ref(false)

  // draft state (survives page navigation)
  const draft = ref({ text: '', attachments: [] })

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
        language: navigator.language || '',
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

    // snapshot live state before clearing (used as fallback for empty responses)
    const liveState = _liveCache.get(chatId)
    _liveCache.delete(chatId)

    if (!_isCurrent(chatId)) {
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

    const content = data.content

    // check if final response has real content
    const hasContent = content.some(
      (b) => (b.type === 'text' && b.text) || b.type === 'tool_use'
    )

    if (hasContent) {
      messages.value.push({
        role: 'assistant',
        content,
        task_id: data.task_id,
        agent_name: data.agent_name,
        timestamp: Date.now(),
      })
    } else if (liveState && liveState.content.length > 0) {
      // empty final response but we have accumulated streaming content (tool uses, text)
      // persist the live state so the user sees what the agent did
      const liveHasContent = liveState.content.some(
        (b) => (b.type === 'text' && b.text) || b.type === 'tool_use'
      )
      if (liveHasContent) {
        messages.value.push({
          role: 'assistant',
          content: liveState.content.map((b) => ({ ...b })),
          task_id: data.task_id,
          agent_name: data.agent_name || liveState.agent_name,
          timestamp: Date.now(),
        })
      }
    }
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
    storage.setItem('chat_session', sessionKey)

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

      // restore from client-side cache if the agent is still streaming
      // and we accumulated events while viewing another session
      const chatId = chatIdFromKey(sessionKey)
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
    const key = `web:${uuidv4()}`
    currentSessionId.value = key
    storage.setItem('chat_session', key)
    messages.value = []
    liveMessage.value = null
    toolRunning.value = false
  }

  function clearLiveCache() {
    _liveCache.clear()
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
    clearLiveCache,
    draft,
  }
})
