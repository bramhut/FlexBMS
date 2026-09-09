<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { csvHeader, csvRow, downloadCsv } from '@/shared/csv'
import { registerFields } from '@/shared/registers'
import { serviceResultLabel } from '@/shared/service'
import { bccDiagnosticNames, bccDiagnosticStatusNames, cellVoltageV } from '@/shared/model'
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
const hvVoltage = (field: 'bat_plus_uV' | 'load_plus_uV') => {
  const voltages = props.snapshot?.hv_voltages
  if (!voltages?.valid) return props.snapshot ? 'Unavailable' : '—'
  if (!fresh.value) return '—'
  return `${cellVoltageV(voltages[field]).toFixed(2)} V`
}
const goodwe = computed(() => props.snapshot?.goodwe_can)
const hexId = (id: number) => `0x${id.toString(16).toUpperCase().padStart(3, '0')}`
const payloadHex = (data: number[]) => data.length ? data.map(value => value.toString(16).toUpperCase().padStart(2, '0')).join(' ') : '—'
const age = (timestamp: number, count: number) => {
  if (count === 0 || timestamp === 0 || !props.status) return 'Never'
  return `${(((props.status.uptime_ms - timestamp) >>> 0) / 1000).toFixed(1)} s ago`
}
const transmitRows = computed(() => goodwe.value?.transmit_frames.map(frame => ({ ...frame, idText: hexId(frame.id), age: age(frame.last_success_ms, frame.success_count) })) ?? [])
const receiveRows = computed(() => goodwe.value?.receive_frames.map(frame => ({ ...frame, idText: hexId(frame.id), age: age(frame.last_seen_ms, frame.count), payload: payloadHex(frame.data) })) ?? [])
const inverter425 = computed(() => goodwe.value?.receive_frames.find(frame => frame.id === 0x425))
const inverter425Voltage = computed(() => {
  const frame = inverter425.value
  return frame && frame.length >= 4 ? `${((frame.data[0] | (frame.data[1] << 8)) / 10).toFixed(1)} V` : 'Waiting'
})
const inverter425CurrentValue = computed(() => {
  const frame = inverter425.value
  if (!frame || frame.length < 4) return null
  const raw = frame.data[2] | (frame.data[3] << 8)
  return (raw > 0x7fff ? raw - 0x10000 : raw) / 10
})
const inverter425Current = computed(() => inverter425CurrentValue.value === null ? 'Waiting' : `${inverter425CurrentValue.value.toFixed(1)} A`)
const reported458Current = computed(() => (goodwe.value?.reported_458_current_deci_a ?? 0) / 10)
const powerDirection = computed(() => {
  if (!goodwe.value) return 'Waiting'
  if (Math.abs(reported458Current.value) < 0.05) return 'Standby'
  return reported458Current.value > 0 ? 'Charging' : 'Discharging'
})
const currentDifference = computed(() => inverter425CurrentValue.value === null ? 'Waiting' : `${Math.abs(reported458Current.value + inverter425CurrentValue.value).toFixed(1)} A`)
const lastFailure = computed(() => {
  const diagnostics = goodwe.value
  if (!diagnostics || diagnostics.transmit_failures === 0) return 'None'
  return `${hexId(diagnostics.last_transmit_failure_id)} · ${age(diagnostics.last_transmit_failure_ms, diagnostics.transmit_failures)}`
})
const protocolActivity = computed(() => ({ 0: 'Synchronizing', 8: 'Idle', 16: 'Receiving', 24: 'Transmitting' }[goodwe.value?.protocol_activity ?? -1] ?? `Code ${goodwe.value?.protocol_activity ?? '—'}`))
const canControllerState = computed(() => {
  const diagnostics = goodwe.value
  if (!diagnostics) return 'Waiting'
  if (diagnostics.bus_off) return 'Bus off'
  if (diagnostics.error_passive) return 'Error passive'
  if (diagnostics.warning) return 'Warning'
  return 'Healthy'
})
const canProtocolState = computed(() => {
  const diagnostics = goodwe.value
  if (!diagnostics) return 'Waiting'
  if (diagnostics.bus_off) return 'bus-off'
  if (diagnostics.error_passive) return 'error-passive'
  return 'error-active'
})

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

    <section class="panel">
      <div class="panel-heading"><div><h2>HV measurements</h2></div><p>Raw high-voltage readings from the AMC3330 isolation amplifiers.</p></div>
      <div class="hv-metrics diagnostic-hv-metrics">
        <div class="primary-metric" :class="{ unavailable: !snapshot?.hv_voltages?.valid }"><span>BAT+ · AMC3330</span><b>{{ hvVoltage('bat_plus_uV') }}</b></div>
        <div class="primary-metric" :class="{ unavailable: !snapshot?.hv_voltages?.valid }"><span>LOAD+ · AMC3330</span><b>{{ hvVoltage('load_plus_uV') }}</b></div>
      </div>
    </section>

    <section class="panel goodwe-diagnostics">
      <div class="panel-heading goodwe-heading"><div><h2>GoodWe CAN diagnostics</h2><p>Live STM32 observations from the inverter CAN bus. Counters reset when the STM32 restarts.</p></div></div>
      <p v-if="!goodwe" class="muted">Waiting for GoodWe CAN diagnostics. Both the updated STM32 and Gateway firmware are required.</p>
      <template v-else>
        <div class="goodwe-power-card">
          <div class="goodwe-power-heading"><div><span class="section-label">Power exchange</span><small>Same physical current, shown using each device's sign convention.</small></div><strong class="direction-chip">{{ powerDirection }}</strong></div>
          <div class="goodwe-power-metrics">
            <div class="primary-metric"><span>BMS reported · 0x458</span><b>{{ reported458Current.toFixed(1) }} A</b><small>Positive is charging</small></div>
            <div class="primary-metric"><span>GoodWe reported · 0x425</span><b>{{ inverter425Current }}</b><small>Negative is charging · candidate decode</small></div>
            <div class="primary-metric"><span>Difference</span><b>{{ currentDifference }}</b><small>After normalizing direction</small></div>
            <div class="primary-metric"><span>Bus voltage</span><b>{{ (goodwe.reported_458_voltage_deci_v / 10).toFixed(1) }} V</b><small>GoodWe: {{ inverter425Voltage }}</small></div>
          </div>
        </div>

        <div class="goodwe-health-metrics">
          <div class="primary-metric"><span>CAN health</span><b :class="{ 'warning-text': goodwe.warning || goodwe.error_passive || goodwe.bus_off }">{{ canControllerState }}</b><small>CAN state: {{ canProtocolState }}</small></div>
          <div class="primary-metric"><span>Transmit cycles</span><b>{{ goodwe.transmit_cycles }}</b><small>Since the STM32 restarted</small></div>
          <div class="primary-metric"><span>TX queue failures</span><b :class="{ 'warning-text': goodwe.transmit_failures > 0 }">{{ goodwe.transmit_failures }}</b><small>Last: {{ lastFailure }}</small></div>
          <div class="primary-metric"><span>Skipped snapshots</span><b :class="{ 'warning-text': goodwe.snapshot_unavailable_cycles > 1 }">{{ goodwe.snapshot_unavailable_cycles }}</b><small>BMS snapshot unavailable</small></div>
          <div class="primary-metric"><span>Error counters</span><b>{{ goodwe.transmit_error_count }} / {{ goodwe.receive_error_count }}</b><small>TEC / REC · log {{ goodwe.error_logging_count }}</small></div>
        </div>

        <details class="goodwe-frame-details">
          <summary><span>Frame details</span><small>{{ transmitRows.length }} transmitted · {{ receiveRows.length }} receive slots</small></summary>
          <div class="goodwe-technical-status"><span>Activity: {{ protocolActivity }}</span><span>Last error: {{ goodwe.protocol_last_error_code }}</span><span>HAL: 0x{{ goodwe.hal_error_code.toString(16).toUpperCase() }}</span><span>TX FIFO free: {{ goodwe.transmit_fifo_free_level }}</span></div>
          <h3>STM32 → GoodWe</h3>
          <div class="table-scroll"><table><thead><tr><th>Frame</th><th>Accepted into TX FIFO</th><th>Last accepted</th></tr></thead><tbody><tr v-for="frame in transmitRows" :key="frame.id"><th>{{ frame.idText }}</th><td>{{ frame.success_count }}</td><td>{{ frame.age }}</td></tr></tbody></table></div>

          <h3>GoodWe → STM32</h3>
          <div class="table-scroll"><table><thead><tr><th>Frame</th><th>Received</th><th>Last received</th><th>Payload</th></tr></thead><tbody><tr v-for="frame in receiveRows" :key="frame.id"><th>{{ frame.idText }}</th><td>{{ frame.count }}</td><td>{{ frame.age }}</td><td class="mono">{{ frame.payload }}</td></tr></tbody></table></div>
          <p class="muted">Request frame 0x45A: {{ goodwe.request_45a_enabled ? 'enabled' : 'disabled' }} · Compatibility frame 0x460: {{ goodwe.compatibility_460_enabled ? 'enabled' : 'disabled' }} · Protocol candidate {{ goodwe.protocol }}</p>
        </details>
      </template>
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
