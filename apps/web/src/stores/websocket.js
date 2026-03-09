import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useChannelsStore } from './channels'
import { useChatStore } from './chat'
import { useTasksStore } from './tasks'
import { useAuthStore } from './auth'

const PING_INTERVAL = 30000
const PONG_TIMEOUT = 10000

export const useWebSocketStore = defineStore('websocket', () => {
  const connected = ref(false)
  let ws = null
  let reconnectTimer = null
  let reconnectDelay = 1000
  let pingTimer = null
  let pongTimer = null

  function connect() {
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return

    const auth = useAuthStore()
    if (!auth.token) return

    teardown()
    connected.value = false

    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
    ws = new WebSocket(`${proto}//${location.host}/ws?token=${auth.token}`)

    ws.onopen = () => {
      connected.value = true
      reconnectDelay = 1000
      startPing()
      syncAfterReconnect()
    }

    ws.onclose = (event) => {
      connected.value = false
      stopPing()
      ws = null
      if (event.code === 4001) {
        useAuthStore().logout()
        return
      }
      scheduleReconnect()
    }

    ws.onerror = () => {}

    ws.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data)
        if (msg.type === 'pong') {
          clearPongTimeout()
          return
        }
        dispatch(msg)
      } catch (e) {
        console.error('[ws] message error:', e)
      }
    }
  }

  function startPing() {
    stopPing()
    pingTimer = setInterval(() => {
      if (!ws || ws.readyState !== WebSocket.OPEN) return
      try {
        ws.send(JSON.stringify({ type: 'ping' }))
      } catch {
        return
      }
      pongTimer = setTimeout(() => {
        if (ws) ws.close()
      }, PONG_TIMEOUT)
    }, PING_INTERVAL)
  }

  function stopPing() {
    if (pingTimer) { clearInterval(pingTimer); pingTimer = null }
    clearPongTimeout()
  }

  function clearPongTimeout() {
    if (pongTimer) { clearTimeout(pongTimer); pongTimer = null }
  }

  function teardown() {
    stopPing()
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null }
    if (ws) {
      ws.onopen = null
      ws.onclose = null
      ws.onerror = null
      ws.onmessage = null
      ws.close()
      ws = null
    }
  }

  function scheduleReconnect() {
    if (reconnectTimer) return
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null
      reconnectDelay = Math.min(reconnectDelay * 2, 30000)
      connect()
    }, reconnectDelay)
  }

  function dispatch(msg) {
    const channels = useChannelsStore()
    const chat = useChatStore()
    const tasks = useTasksStore()

    switch (msg.type) {
      case 'channel:status':
        channels.onChannelStatus(msg.data)
        break
      case 'chat:message':
        chat.onMessage(msg.data)
        break
      case 'chat:thinking':
        chat.onThinking(msg.data)
        tasks.onThinking(msg.data)
        break
      case 'chat:typing':
        chat.onTyping(msg.data)
        break
      case 'chat:stream':
        chat.onStream(msg.data)
        break
      case 'chat:stream_end':
        chat.onStreamEnd(msg.data)
        break
      case 'chat:tool_use':
        chat.onToolUse(msg.data)
        tasks.onToolUse(msg.data)
        break
      case 'chat:user_message':
        chat.onUserMessage(msg.data)
        break
      case 'chat:transcription':
        chat.onTranscription(msg.data)
        break
      case 'chat:warning':
        chat.onWarning(msg.data)
        break
      case 'sessions:updated':
        chat.onSessionsUpdated()
        break
      case 'task:created':
        tasks.onTaskCreated(msg.data)
        break
      case 'task:updated':
        tasks.onTaskUpdated(msg.data)
        break
    }
  }

  function send(type, data) {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ type, data }))
    }
  }

  function syncAfterReconnect() {
    const tasks = useTasksStore()
    const chat = useChatStore()
    tasks.loadTasks()
    chat.loadSessions()
    chat.loadHistory(chat.currentSessionId)
  }

  function disconnect() {
    teardown()
    connected.value = false
  }

  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') {
        if (!ws || ws.readyState !== WebSocket.OPEN) {
          connect()
        }
      }
    })
  }

  return { connected, connect, disconnect, send }
})
