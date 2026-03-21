<script setup>
import { ref, watch, onMounted } from 'vue'
import DataTable from 'primevue/datatable'
import Column from 'primevue/column'
import Button from 'primevue/button'
import Dialog from 'primevue/dialog'
import Tag from 'primevue/tag'
import { useToast } from 'primevue/usetoast'
import { useApi } from '../composables/useApi'
import DynamicForm from '../components/common/DynamicForm.vue'

const api = useApi()
const toast = useToast()
const jobs = ref([])
const showCreateDialog = ref(false)
const showEditDialog = ref(false)
const showDeleteConfirm = ref(false)
const deleteJobId = ref('')
const loading = ref(true)

const jobSchema = [
  { name: 'name', type: 'text', label: 'Name', required: true },
  { name: 'message', type: 'text', label: 'Message', required: true },
  {
    name: 'type',
    type: 'select',
    label: 'Schedule Type',
    options: [
      { label: 'Interval', value: 'every' },
      { label: 'Cron Expression', value: 'cron' },
      { label: 'One-time', value: 'at' },
    ],
  },
  { name: 'every_seconds', type: 'int', label: 'Interval (seconds)', visible_when: { type: 'every' } },
  { name: 'cron_expr', type: 'text', label: 'Cron Expression', placeholder: '0 9 * * *', visible_when: { type: 'cron' } },
  { name: 'at', type: 'datetime', label: 'Date/Time', visible_when: { type: 'at' } },
]

const emptyJob = () => ({
  name: '',
  message: '',
  type: 'every',
  every_seconds: 3600,
  cron_expr: '',
  at: null,
})

const newJob = ref(emptyJob())
const editJob = ref(emptyJob())
const editJobId = ref('')

watch(() => newJob.value.type, (val) => {
  if (val === 'at' && !newJob.value.at) {
    newJob.value.at = new Date(Date.now() + 30 * 60 * 1000)
  }
})

watch(() => editJob.value.type, (val) => {
  if (val === 'at' && !editJob.value.at) {
    editJob.value.at = new Date(Date.now() + 30 * 60 * 1000)
  }
})

function toUtcISO(date) {
  return date.toISOString()
}

onMounted(loadJobs)

async function loadJobs() {
  try {
    jobs.value = await api.get('/scheduler/jobs')
  } catch (e) {
    toast.add({ severity: 'error', summary: 'Error', detail: 'Failed to load jobs', life: 3000 })
  } finally {
    loading.value = false
  }
}

function buildScheduleBody(form) {
  const body = {
    name: form.name,
    message: form.message,
  }
  if (form.type === 'every') body.every_seconds = form.every_seconds
  if (form.type === 'cron') body.cron_expr = form.cron_expr
  if (form.type === 'at' && form.at) body.at = toUtcISO(form.at)
  return body
}

async function createJob() {
  if (!newJob.value.message?.trim()) {
    toast.add({ severity: 'warn', summary: 'Validation', detail: 'Message is required', life: 3000 })
    return
  }

  try {
    await api.post('/scheduler/jobs', buildScheduleBody(newJob.value))
    toast.add({ severity: 'success', summary: 'Created', detail: 'Job created', life: 2000 })
    showCreateDialog.value = false
    newJob.value = emptyJob()
    await loadJobs()
  } catch (e) {
    toast.add({ severity: 'error', summary: 'Error', detail: e.message, life: 3000 })
  }
}

function openEditDialog(job) {
  editJobId.value = job.id
  editJob.value = {
    name: job.name || '',
    message: job.payload?.message || '',
    type: job.schedule.kind || 'every',
    every_seconds: job.schedule.kind === 'every' ? Math.round(job.schedule.every_ms / 1000) : 3600,
    cron_expr: job.schedule.kind === 'cron' ? job.schedule.expr : '',
    at: job.schedule.kind === 'at' ? new Date(job.schedule.at_ms) : null,
  }
  showEditDialog.value = true
}

async function updateJob() {
  if (!editJob.value.message?.trim()) {
    toast.add({ severity: 'warn', summary: 'Validation', detail: 'Message is required', life: 3000 })
    return
  }

  try {
    await api.put(`/scheduler/jobs/${editJobId.value}`, buildScheduleBody(editJob.value))
    toast.add({ severity: 'success', summary: 'Updated', detail: 'Job updated', life: 2000 })
    showEditDialog.value = false
    await loadJobs()
  } catch (e) {
    toast.add({ severity: 'error', summary: 'Error', detail: e.message, life: 3000 })
  }
}

function promptDeleteJob(id) {
  deleteJobId.value = id
  showDeleteConfirm.value = true
}

async function confirmDeleteJob() {
  showDeleteConfirm.value = false
  try {
    await api.del(`/scheduler/jobs/${deleteJobId.value}`)
    toast.add({ severity: 'success', summary: 'Deleted', detail: 'Job deleted', life: 2000 })
    await loadJobs()
  } catch (e) {
    toast.add({ severity: 'error', summary: 'Error', detail: e.message, life: 3000 })
  }
}

function formatSchedule(job) {
  if (job.schedule.kind === 'cron') return `cron: ${job.schedule.expr}`
  if (job.schedule.kind === 'every') return `every ${job.schedule.every_ms / 1000}s`
  if (job.schedule.kind === 'at') return `at: ${new Date(job.schedule.at_ms).toLocaleString()}`
  return job.schedule.kind
}

function statusSeverity(status) {
  const map = { running: 'success', active: 'success', stopped: 'warn', error: 'danger', pending: 'info' }
  return map[status] || 'secondary'
}
</script>

<template>
  <div class="scheduler-page">
    <div class="page-header">
      <h2>Scheduler</h2>
      <Button label="Add Job" icon="pi pi-plus" size="small" @click="showCreateDialog = true" />
    </div>

    <div v-if="loading" class="page-loading">
      <i class="pi pi-spin pi-spinner" style="font-size: 1.5rem"></i>
    </div>

    <div v-else class="table-container">
      <DataTable :value="jobs" striped-rows show-gridlines removable-sort>
        <template #empty>
          <div class="empty-state">
            <i class="pi pi-clock" style="font-size: 2rem; color: var(--p-text-muted-color)"></i>
            <p>No scheduled jobs</p>
          </div>
        </template>
        <Column field="name" header="Name" sortable />
        <Column header="Schedule">
          <template #body="{ data }">
            <code class="schedule-code">{{ formatSchedule(data) }}</code>
          </template>
        </Column>
        <Column field="payload.message" header="Message" />
        <Column header="Status">
          <template #body="{ data }">
            <Tag :value="data.state.status" :severity="statusSeverity(data.state.status)" />
          </template>
        </Column>
        <Column field="state.run_count" header="Runs" sortable />
        <Column header="Actions" style="width: 7rem">
          <template #body="{ data }">
            <div class="action-buttons">
              <Button icon="pi pi-pencil" severity="secondary" text size="small" @click="openEditDialog(data)" />
              <Button icon="pi pi-trash" severity="danger" text size="small" @click="promptDeleteJob(data.id)" />
            </div>
          </template>
        </Column>
      </DataTable>
    </div>

    <Dialog v-model:visible="showCreateDialog" header="New Job" modal :style="{ width: '500px' }" :breakpoints="{ '768px': '95vw' }">
      <DynamicForm :schema="jobSchema" v-model="newJob" />
      <div class="dialog-footer">
        <Button label="Cancel" severity="secondary" text @click="showCreateDialog = false" />
        <Button label="Create" icon="pi pi-check" @click="createJob" />
      </div>
    </Dialog>

    <Dialog v-model:visible="showEditDialog" header="Edit Job" modal :style="{ width: '500px' }" :breakpoints="{ '768px': '95vw' }">
      <DynamicForm :schema="jobSchema" v-model="editJob" />
      <div class="dialog-footer">
        <Button label="Cancel" severity="secondary" text @click="showEditDialog = false" />
        <Button label="Save" icon="pi pi-save" @click="updateJob" />
      </div>
    </Dialog>

    <Dialog v-model:visible="showDeleteConfirm" header="Confirm Delete" :modal="true" :style="{ width: '24rem' }" :breakpoints="{ '768px': '90vw' }">
      <p>Delete this scheduled job?</p>
      <template #footer>
        <Button label="Cancel" severity="secondary" text size="small" @click="showDeleteConfirm = false" />
        <Button label="Delete" icon="pi pi-trash" severity="danger" size="small" @click="confirmDeleteJob" />
      </template>
    </Dialog>
  </div>
</template>

<style scoped>
.scheduler-page {
  padding: 0;
}

.page-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.75rem 1rem;
  border-bottom: 1px solid var(--p-content-border-color);
  background: var(--p-content-background);
}

.page-header h2 {
  margin: 0;
  font-size: 1.1rem;
}

.table-container {
  padding: 1rem;
}

.schedule-code {
  font-family: 'SF Mono', ui-monospace, monospace;
  font-size: 0.8rem;
  background: var(--p-content-hover-background);
  color: var(--p-text-color);
  padding: 0.2rem 0.5rem;
  border-radius: 0.25rem;
}

.page-loading {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 3rem 1rem;
  color: var(--p-text-muted-color);
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 3rem 1rem;
  color: var(--p-text-muted-color);
  gap: 0.75rem;
}

.action-buttons {
  display: flex;
  gap: 0.25rem;
}

.dialog-footer {
  display: flex;
  justify-content: flex-end;
  gap: 0.5rem;
  margin-top: 1rem;
}
</style>
