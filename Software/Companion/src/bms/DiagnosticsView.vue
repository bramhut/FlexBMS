<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { csvHeader, csvRow, downloadCsv } from '@/shared/csv'
import { registerFields } from '@/shared/registers'
import { serviceResultLabel } from '@/shared/service'
import { bccDiagnosticNames, bccDiagnosticStatusNames } from '@/shared/model'
import type { BccDiagnosticReport, BmsTransport, Capabilities, ServiceResponse, Snapshot, Status } from '@/transports/Transport'

const props = defineProps<{ snapshot: Snapshot | null; status: Status | null; transport: BmsTransport; capabilities: Capabilities; connected: boolean; diagnosticReports: BccDiagnosticReport[] }>()
const lines = ref<string[]>([])
const logging = ref(false)
const slave = ref(0)
const registerText = ref('03')
const registerValue = ref<number | null>(null)
const registerError = ref('')
const result = ref('')
const fresh = computed(() => Boolean(props.snapshot && props.status?.measurements_fresh))
const registerKey = computed(() => registerText.value.trim().toUpperCase().padStart(2, '0'))
const fields = computed(() => registerFields[registerKey.value] ?? [])
const bits = computed(() => registerValue.value === null ? '' : registerValue.value.toString(2).padStart(16, '0'))
const availability = (capability: keyof Capabilities) => props.capabilities[capability] ? '' : 'Unavailable in the current Gateway state.'
const reportRows = computed(() => props.diagnosticReports.map(report => ({
  ...report,
  failedNames: bccDiagnosticNames.filter((_, bit) => (report.failed_checks & (1 << bit)) !== 0),
  statusName: bccDiagnosticStatusNames[report.status_code] ?? `BCC status ${report.status_code}`,
  failedName: bccDiagnosticNames[report.failed_diagnostic] ?? 'diagnostic execution',
})))

function describe(response: ServiceResponse): string {
  if (response.data?.value !== undefined) return `${serviceResultLabel(response.result)}: 0x${response.data.value.toString(16).padStart(4, '0').toUpperCase()}`
  return serviceResultLabel(response.result)
}

function appendSnapshot(snapshot: Snapshot): void {
  if (!snapshot.status.measurements_fresh) return
  if (lines.value.length === 0) lines.value = [csvHeader(snapshot).join(',')]
  lines.value.push(csvRow(snapshot).join(','))
}

function startLogging(): void {
  if (props.snapshot && fresh.value) {
    lines.value = []
    appendSnapshot(props.snapshot)
    logging.value = true
  }
}

function stopLogging(): void { logging.value = false }

async function readRegister(): Promise<void> {
  const value = Number.parseInt(registerText.value, 16)
  if (!Number.isInteger(value) || value < 0 || value > 0xff || !Number.isInteger(slave.value) || slave.value < 0 || slave.value > 255) {
    registerError.value = 'Enter a zero-based slave index and a hexadecimal register address from 00 to FF.'
    return
  }
  registerError.value = ''
  const response = await props.transport.request('read_register', { slave_index: slave.value, register: value })
  result.value = describe(response)
  registerValue.value = response.data?.value ?? null
}

watch(() => props.snapshot, snapshot => { if (logging.value && snapshot) appendSnapshot(snapshot) })
</script>

<template>
  <main class="stack diagnostics-page">
    <section class="panel diagnostic-report-panel">
      <div class="panel-heading"><div><h2>Last BCC diagnostic report</h2></div><p>Per-slave details from the most recent startup diagnostic run.</p></div>
      <p v-if="!diagnosticReports.length" class="muted">No diagnostic report is available. A report appears when the BMS reports a BCC diagnostic failure.</p>
      <div v-else class="table-scroll"><table><thead><tr><th>Slave</th><th>CID</th><th>Failed checks</th><th>Low-level result</th></tr></thead><tbody><tr v-for="report in reportRows" :key="report.slave_index"><th>Slave {{ report.slave_index + 1 }}</th><td>{{ report.cid }}</td><td>{{ report.failedNames.length ? report.failedNames.join(', ') : 'None recorded' }}</td><td>{{ report.status_code === 0 ? 'Functional result' : `${report.statusName} during ${report.failedName}` }}</td></tr></tbody></table></div>
    </section>

    <section class="panel logging-panel">
      <div class="panel-heading"><div><h2>CSV logging</h2></div><p>No data is sent to the Gateway or stored on the BMS.</p></div>
      <div class="button-row"><button class="primary" :disabled="!fresh || logging" @click="startLogging">Start logging</button><button :disabled="!logging" @click="stopLogging">Stop logging</button><button :disabled="lines.length < 2" @click="downloadCsv(lines)">Download {{ Math.max(0, lines.length - 1) }} rows</button></div>
    </section>

    <section class="panel register-panel">
      <div class="panel-heading"><div><h2>Read BCC register</h2></div><p>Read-only. Slave indexes are zero-based.</p></div>
      <div class="register-form"><label>Slave<input v-model.number="slave" type="number" min="0" max="255"></label><label>Register (hex)<input v-model="registerText" maxlength="2" inputmode="text"></label><button :disabled="!capabilities.read_register" @click="readRegister">Read register</button></div>
      <p v-if="registerError" class="warning-text">{{ registerError }}</p>
      <div v-if="registerValue !== null" class="register-result"><p><b>0x{{ registerKey }}</b> = <b>0x{{ registerValue.toString(16).padStart(4, '0').toUpperCase() }}</b></p><p class="mono">{{ bits }}</p><ul v-if="fields.length"><li v-for="field in fields" :key="field.name">{{ field.name }} ({{ field.bits }} bit{{ field.bits === 1 ? '' : 's' }})</li></ul><p v-else class="muted">No compact field description is available for this address.</p></div>
      <small>{{ availability('read_register') }}</small>
      <p v-if="result" class="action-result">{{ result }}</p>
    </section>
  </main>
</template>
