<script setup>
import { computed, ref, watch, onUnmounted } from 'vue'
import Button from 'primevue/button'
import { useApi } from '../../composables/useApi'

const props = defineProps({
  path: { type: String, default: '' },
  type: { type: String, default: '' },
  mime: { type: String, default: '' },
  size: { type: Number, default: 0 },
  url: { type: String, default: '' },
})

const api = useApi()
const downloading = ref(false)
const blobUrl = ref('')
const loadingPreview = ref(false)

const isPublic = computed(() => typeof props.path === 'string' && props.path.startsWith('public/'))
const filename = computed(() => props.path.split('/').pop())

const mediaSrc = computed(() => {
  if (isPublic.value) return props.url
  return blobUrl.value
})

const formattedSize = computed(() => {
  const bytes = props.size
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
})

// For non-public files, fetch via authenticated API and create blob URL
watch(() => props.path, async (path) => {
  if (blobUrl.value) {
    URL.revokeObjectURL(blobUrl.value)
    blobUrl.value = ''
  }
  if (!path || isPublic.value) return
  loadingPreview.value = true
  try {
    const { useAuthStore } = await import('../../stores/auth')
    const auth = useAuthStore()
    const token = auth.token
    const url = `${window.location.origin}/api/files/download/${path.startsWith('/') ? path.slice(1) : path}`
    const headers = {}
    if (token) headers['Authorization'] = `Bearer ${token}`
    const res = await fetch(url, { headers })
    if (res.ok) {
      const blob = await res.blob()
      blobUrl.value = URL.createObjectURL(blob)
    }
  } catch (e) {
    // silent — preview just won't show
  } finally {
    loadingPreview.value = false
  }
}, { immediate: true })

onUnmounted(() => {
  if (blobUrl.value) URL.revokeObjectURL(blobUrl.value)
})

async function onDownload() {
  if (!props.path) return
  downloading.value = true
  try {
    await api.downloadFile(props.path, filename.value)
  } finally {
    downloading.value = false
  }
}
</script>

<template>
  <div class="media-container">
    <div class="media-toolbar">
      <span class="media-path">{{ path }}</span>
      <div class="media-info">
        <span class="media-meta">{{ mime }} &middot; {{ formattedSize }}</span>
        <Button label="Download" icon="pi pi-download" size="small" severity="secondary" :loading="downloading" @click="onDownload" />
      </div>
    </div>

    <div class="media-preview">
      <i v-if="loadingPreview" class="pi pi-spin pi-spinner" style="font-size: 1.5rem; color: var(--p-text-muted-color)"></i>
      <template v-else-if="mediaSrc">
        <img
          v-if="type === 'image'"
          :src="mediaSrc"
          :alt="filename"
          class="preview-image"
        />
        <video
          v-else-if="type === 'video'"
          :src="mediaSrc"
          controls
          class="preview-video"
        />
        <audio
          v-else-if="type === 'audio'"
          :src="mediaSrc"
          controls
          class="preview-audio"
        />
      </template>
      <p v-else class="media-hint">Unable to load preview.</p>
    </div>
  </div>
</template>

<style scoped>
.media-container {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.media-toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.5rem 1rem;
  border-bottom: 1px solid var(--p-content-border-color);
  background: var(--p-content-background);
}

.media-path {
  font-family: monospace;
  font-size: 0.85rem;
  color: var(--p-text-muted-color);
  overflow-wrap: break-word;
  word-break: break-all;
  min-width: 0;
}

.media-info {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.media-meta {
  font-size: 0.8rem;
  color: var(--p-text-muted-color);
}

.media-preview {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 1.5rem;
  overflow: auto;
  background: var(--p-surface-50);
}

:global(.dark-mode) .media-preview {
  background: var(--p-surface-900);
}

.preview-image {
  max-width: 100%;
  max-height: 100%;
  object-fit: contain;
  border-radius: 0.5rem;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.12);
}

.preview-video {
  max-width: 100%;
  max-height: 100%;
  border-radius: 0.5rem;
}

.preview-audio {
  width: 100%;
  max-width: 500px;
}

.media-hint {
  margin: 0;
  font-size: 0.9rem;
  color: var(--p-text-muted-color);
  text-align: center;
}

@media (max-width: 768px) {
  .media-toolbar {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.5rem;
    padding: 0.75rem;
  }

  .media-preview {
    padding: 1rem;
  }
}
</style>
