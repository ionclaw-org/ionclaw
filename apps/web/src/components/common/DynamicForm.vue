<script setup>
import { computed, ref } from 'vue'
import { v4 as uuidv4 } from 'uuid'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import DynamicFormField from './DynamicFormField.vue'

const props = defineProps({
  schema: { type: Array, required: true },
  modelValue: { type: Object, required: true },
  references: { type: Object, default: () => ({}) },
})

const emit = defineEmits(['update:modelValue'])

function update(name, value) {
  emit('update:modelValue', { ...props.modelValue, [name]: value })
}

function isVisible(field) {
  if (!field.visible_when) return true
  for (const [key, expected] of Object.entries(field.visible_when)) {
    const actual = props.modelValue[key]
    if (Array.isArray(expected)) {
      if (!expected.includes(actual)) return false
    } else {
      if (actual !== expected) return false
    }
  }
  return true
}

const visibleFields = computed(() => props.schema.filter(isVisible))

// split into a toggles group and an inputs group so they render as separate sections
const toggleFields = computed(() => visibleFields.value.filter((f) => f.type === 'bool'))
const inputFields = computed(() => visibleFields.value.filter((f) => f.type !== 'bool'))

const pendingGenerateField = ref(null)

function requestGenerate(fieldName) {
  pendingGenerateField.value = fieldName
}

function confirmGenerate() {
  update(pendingGenerateField.value, uuidv4())
  pendingGenerateField.value = null
}
</script>

<template>
  <div class="dynamic-form">
    <div v-if="toggleFields.length" class="field-group">
      <DynamicFormField
        v-for="field in toggleFields"
        :key="field.name"
        :field="field"
        :value="modelValue[field.name]"
        :references="references"
        @update="update(field.name, $event)"
        @generate="requestGenerate(field.name)"
      />
    </div>

    <div v-if="inputFields.length" class="field-group">
      <DynamicFormField
        v-for="field in inputFields"
        :key="field.name"
        :field="field"
        :value="modelValue[field.name]"
        :references="references"
        @update="update(field.name, $event)"
        @generate="requestGenerate(field.name)"
      />
    </div>
  </div>

  <Dialog
    :visible="pendingGenerateField !== null"
    header="Generate New Key"
    :modal="true"
    :style="{ width: '22rem' }"
    :breakpoints="{ '768px': '90vw' }"
    @update:visible="pendingGenerateField = null"
  >
    <p style="margin: 0; font-size: 0.9rem">
      This will replace the current key with a new UUID. Any clients using the old key will stop working.
    </p>
    <template #footer>
      <Button label="Cancel" severity="secondary" text size="small" @click="pendingGenerateField = null" />
      <Button label="Generate" icon="pi pi-refresh" size="small" @click="confirmGenerate" />
    </template>
  </Dialog>
</template>

<style scoped>
.dynamic-form {
  display: flex;
  flex-direction: column;
}

/* each group flows its fields into responsive columns: one column when narrow, several when wide */
.field-group {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  column-gap: 1.5rem;
  align-items: start;
}

/* clear break between the toggles group and the inputs group */
.field-group + .field-group {
  margin-top: 0.5rem;
}
</style>
