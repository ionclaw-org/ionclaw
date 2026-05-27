<script setup>
import InputText from 'primevue/inputtext'
import InputNumber from 'primevue/inputnumber'
import Textarea from 'primevue/textarea'
import Password from 'primevue/password'
import ToggleSwitch from 'primevue/toggleswitch'
import Select from 'primevue/select'
import DatePicker from 'primevue/datepicker'
import ColorPicker from 'primevue/colorpicker'
import Button from 'primevue/button'

const props = defineProps({
  field: { type: Object, required: true },
  value: { default: undefined },
  references: { type: Object, default: () => ({}) },
})

defineEmits(['update', 'generate'])

function humanize(name) {
  return name.replace(/[_-]/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase())
}

function refOptions(type) {
  const items = props.references[type + 's'] || []
  return [{ label: '(none)', value: '' }, ...items.map((n) => ({ label: humanize(n), value: n }))]
}
</script>

<template>
  <div class="form-group">
    <label>
      {{ field.label }}
      <span v-if="field.required" class="required">*</span>
    </label>

    <!-- text -->
    <InputText
      v-if="field.type === 'text' || field.type === 'email'"
      :model-value="value ?? ''"
      :type="field.type === 'email' ? 'email' : 'text'"
      :placeholder="field.placeholder || ''"
      autocapitalize="none"
      autocomplete="off"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- int -->
    <InputNumber
      v-else-if="field.type === 'int'"
      :model-value="value ?? 0"
      :use-grouping="false"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- float -->
    <InputNumber
      v-else-if="field.type === 'float'"
      :model-value="value ?? 0"
      :min-fraction-digits="1"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- bool -->
    <ToggleSwitch
      v-else-if="field.type === 'bool'"
      :model-value="value ?? false"
      @update:model-value="$emit('update', $event)"
    />

    <!-- secret with generate button -->
    <div v-else-if="field.type === 'secret' && field.generate" class="secret-generate-row">
      <Password
        :model-value="value ?? ''"
        :feedback="false"
        toggle-mask
        :placeholder="field.placeholder ?? 'Leave empty to keep current'"
        class="flex-1"
        input-class="w-full"
        :input-props="{ autocomplete: 'off', 'data-1p-ignore': '' }"
        @update:model-value="$emit('update', $event)"
      />
      <Button
        v-tooltip.top="'Generate new UUID'"
        icon="pi pi-refresh"
        severity="secondary"
        size="small"
        @click="$emit('generate')"
      />
    </div>

    <!-- secret -->
    <Password
      v-else-if="field.type === 'secret'"
      :model-value="value ?? ''"
      :feedback="false"
      toggle-mask
      :placeholder="field.placeholder ?? 'Leave empty to keep current'"
      class="w-full"
      input-class="w-full"
      :input-props="{ autocomplete: 'off', 'data-1p-ignore': '' }"
      @update:model-value="$emit('update', $event)"
    />

    <!-- long_text -->
    <Textarea
      v-else-if="field.type === 'long_text'"
      :model-value="value ?? ''"
      :placeholder="field.placeholder || ''"
      rows="4"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- select -->
    <Select
      v-else-if="field.type === 'select'"
      :model-value="value ?? ''"
      :options="field.options || []"
      option-label="label"
      option-value="value"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- credential -->
    <Select
      v-else-if="field.type === 'credential'"
      :model-value="value ?? ''"
      :options="refOptions('credential')"
      option-label="label"
      option-value="value"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- provider -->
    <Select
      v-else-if="field.type === 'provider'"
      :model-value="value ?? ''"
      :options="refOptions('provider')"
      option-label="label"
      option-value="value"
      class="w-full"
      @update:model-value="$emit('update', $event)"
    />

    <!-- datetime -->
    <DatePicker
      v-else-if="field.type === 'datetime'"
      :model-value="value"
      show-time
      hour-format="24"
      show-icon
      fluid
      date-format="yy-mm-dd"
      @update:model-value="$emit('update', $event)"
    />

    <!-- date -->
    <DatePicker
      v-else-if="field.type === 'date'"
      :model-value="value"
      show-icon
      fluid
      date-format="yy-mm-dd"
      @update:model-value="$emit('update', $event)"
    />

    <!-- time -->
    <DatePicker
      v-else-if="field.type === 'time'"
      :model-value="value"
      time-only
      hour-format="24"
      show-icon
      fluid
      @update:model-value="$emit('update', $event)"
    />

    <!-- color -->
    <ColorPicker
      v-else-if="field.type === 'color'"
      :model-value="value ?? ''"
      @update:model-value="$emit('update', $event)"
    />
  </div>
</template>

<style scoped>
.form-group {
  margin-bottom: 1rem;
}

.form-group label {
  display: block;
  margin-bottom: 0.4rem;
  font-size: 0.85rem;
  font-weight: 600;
}

.required {
  color: var(--p-red-500);
  margin-left: 0.15rem;
}

.secret-generate-row {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}
</style>
