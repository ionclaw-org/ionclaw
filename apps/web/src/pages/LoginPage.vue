<script setup>
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import Card from 'primevue/card'
import Button from 'primevue/button'
import Message from 'primevue/message'
import { useAuthStore } from '../stores/auth'
import { useDark, toggleDark } from '../composables/useDark'
import DynamicForm from '../components/common/DynamicForm.vue'

const router = useRouter()
const authStore = useAuthStore()
const darkMode = useDark()
const base = import.meta.env.BASE_URL
const logoSrc = computed(() => darkMode.value ? `${base}logo-dark.png` : `${base}logo.png`)

const loginSchema = [
  { name: 'username', type: 'text', label: 'Username', required: true },
  { name: 'password', type: 'secret', label: 'Password', required: true, placeholder: '' },
]

const formData = ref({ username: '', password: '' })
const error = ref('')
const loading = ref(false)

async function handleLogin() {
  error.value = ''
  loading.value = true
  try {
    await authStore.login(formData.value.username, formData.value.password)
    router.push('/')
  } catch (e) {
    error.value = e.message || 'Login failed'
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <div class="login-page">
    <Card class="login-card">
      <template #header>
        <div class="login-header">
          <img :src="logoSrc" alt="IonClaw" class="login-logo" />
        </div>
      </template>
      <template #content>
        <form @submit.prevent="handleLogin" class="login-form">
          <Message v-if="error" severity="error" :closable="false">{{ error }}</Message>
          <DynamicForm :schema="loginSchema" v-model="formData" />
          <Button type="submit" label="Login" icon="pi pi-sign-in" :loading="loading" class="w-full" />
        </form>
      </template>
    </Card>
    <Button
      :icon="darkMode ? 'pi pi-sun' : 'pi pi-moon'"
      severity="secondary"
      text
      rounded
      class="theme-toggle"
      @click="toggleDark"
    />
  </div>
</template>

<style scoped>
.login-page {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  padding: 1rem;
  background: inherit;
}

.login-card {
  width: 100%;
  max-width: 400px;
}

.login-header {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 2rem 1rem 1rem;
}

.login-logo {
  height: 60px;
  object-fit: contain;
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.theme-toggle {
  position: absolute;
  top: 1rem;
  right: 1rem;
}
</style>
