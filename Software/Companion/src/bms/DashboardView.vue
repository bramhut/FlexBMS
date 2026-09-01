<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import ServiceView from './ServiceView.vue'
import RecentChangesList from './RecentChangesList.vue'
import { bmsFaultNames, bmsStateName, cellVoltageV, currentA, hvReasonNames, hvStateName, icCelsius, ntcCelsius, setBits, socPercent, warningDisplayNames } from '@/shared/model'
import { csvHeader, csvRow, downloadCsv } from '@/shared/csv'
import { serviceResultLabel } from '@/shared/service'
import type { BmsTransport, Capabilities, GatewayStatus, RecordedControllerEvent, ServiceResponse, Snapshot, Status } from '@/transports/Transport'

const props = defineProps<{ snapshot: Snapshot | null; status: Status | null; transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus; recentEvents: RecordedControllerEvent[] }>()
const emit = defineEmits<{ showAll: [] }>()
const lines = ref<string[]>([])
const logging = ref(false)
const requested = ref(false)
const changingRunRequest = ref(false)
const runResult = ref('')
const fresh = computed(() => Boolean(props.snapshot && props.status?.measurements_fresh))
const measurement = (value: string) => fresh.value ? value : '—'
type AttentionItem = { label: string; domain: 'BMS' | 'HV' | 'System'; state: 'live' | 'pending' | 'warning' }

const friendlyName = (name: string) => name.toLowerCase().replace(/_/g, ' ').replace(/\b[a-z]/g, (letter: string) => letter.toUpperCase())
const attentionItems = computed<AttentionItem[]>(() => {
  const status = props.status
  if (!status) return []
  const items: AttentionItem[] = []
  const addErrors = (active: number, latched: number, names: string[], domain: 'BMS' | 'HV') => {
    names.forEach((name, bit) => {
      const bitMask = 1 << bit
      if ((active & bitMask) !== 0) items.push({ label: friendlyName(name), domain, state: 'live' })
      else if ((latched & bitMask) !== 0) items.push({ label: friendlyName(name), domain, state: 'pending' })
    })
  }
  addErrors(status.bms_active_errors, status.bms_latched_errors, bmsFaultNames, 'BMS')
  addErrors(status.hv_active_errors, status.hv_latched_errors, hvReasonNames, 'HV')
  setBits(status.warnings, warningDisplayNames).forEach(name => items.push({
    label: name,
    domain: 'System',
    state: 'warning',
  }))
  return items
})
const liveIssueCount = computed(() => attentionItems.value.filter(item => item.state === 'live').length)
const pendingIssueCount = computed(() => attentionItems.value.filter(item => item.state === 'pending').length)
const warningCount = computed(() => attentionItems.value.filter(item => item.state === 'warning').length)
const acknowledgementPending = computed(() => {
  const status = props.status
  return Boolean(status && liveIssueCount.value === 0 &&
    (pendingIssueCount.value > 0 || (status.warnings & (1 << 0)) !== 0))
})
const cellDeltaMv = computed(() => props.snapshot ? (props.snapshot.pack.max_cell_uV - props.snapshot.pack.min_cell_uV) / 1000 : 0)
const currentValue = computed(() => {
  if (!props.status?.current_sensing_enabled) return 'Disabled'
  return props.snapshot ? measurement(`${currentA(props.snapshot.pack.pack_current_raw).toFixed(2)} A`) : '—'
})
const hvVoltage = (field: 'bat_plus_uV' | 'load_plus_uV') => {
  const voltages = props.snapshot?.hv_voltages
  if (!voltages?.valid) return props.snapshot ? 'Unavailable' : '—'
  return measurement(`${cellVoltageV(voltages[field]).toFixed(2)} V`)
}
const hvDisplay = computed(() => {
  const status = props.status
  if (!status) return { detail: 'Waiting', ready: false }

  const state = hvStateName(status.hv_state)
  if (status.hv_state !== 0) return { detail: state, ready: false }

  const ready = (status.flags & (1 << 0)) !== 0
  return { detail: `${state} · ${ready ? 'Ready' : 'Not ready'}`, ready }
})
const socValue = computed(() => props.snapshot && props.status?.soc_valid ? measurement(`${socPercent(props.snapshot.pack.soc_raw).toFixed(1)} %`) : 'Unavailable')
const socCalibration = computed(() => {
  const status = props.status
  if (!status?.current_sensing_enabled) return 'Current sensing disabled'
  const unixTime = status.soc_last_calibration_unix_s
  if (unixTime === undefined) return 'No recorded full calibration'
  const ageSeconds = Math.max(0, Math.floor(Date.now() / 1000) - unixTime)
  const age = ageSeconds < 3600 ? `${Math.floor(ageSeconds / 60)} min ago` : ageSeconds < 86400 ? `${Math.floor(ageSeconds / 3600)} h ago` : `${Math.floor(ageSeconds / 86400)} d ago`
  return `${new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'short' }).format(new Date(unixTime * 1000))} (${age})`
})
const controlAvailability = (capability: keyof Capabilities) => props.capabilities[capability] ? '' : 'Unavailable in the current Gateway state.'
function describeControlResponse(response: ServiceResponse): string { return serviceResultLabel(response.result) }
async function setRunRequest(): Promise<void> {
  const requestedValue = requested.value
  changingRunRequest.value = true
  const response = await props.transport.request('set_run_request', { requested: requestedValue })
  runResult.value = `Run: ${describeControlResponse(response)}`
  changingRunRequest.value = false
  if (response.result !== 'ok') requested.value = props.status?.run_request ?? false
}
function appendSnapshot(snapshot: Snapshot): void {
  if (!snapshot.status.measurements_fresh) return
  if (lines.value.length === 0) lines.value = [csvHeader(snapshot).join(',')]
  lines.value.push(csvRow(snapshot).join(','))
}
function startLogging(): void { if (props.snapshot && fresh.value) { lines.value = []; appendSnapshot(props.snapshot); logging.value = true } }
function stopLogging(): void { logging.value = false }
watch(() => props.snapshot, snapshot => { if (logging.value && snapshot) appendSnapshot(snapshot) })
watch(() => props.status?.run_request, value => { if (!changingRunRequest.value) requested.value = value ?? false }, { immediate: true })
</script>

<template>
  <main class="dashboard">
    <section class="dashboard-top">
      <section class="panel overview">
        <div class="overview-section">
          <span class="section-label">Operating</span>
          <div class="operating-row">
            <div class="state-chips">
              <span class="state-chip"><b>BMS</b>{{ status ? bmsStateName(status.bms_state) : 'Waiting' }}</span>
              <span class="state-chip" :class="{ ready: hvDisplay.ready }"><b>HV</b>{{ hvDisplay.detail }}</span>
            </div>
            <div class="context-control">
              <label class="ios-switch"><input v-model="requested" type="checkbox" role="switch" :disabled="!capabilities.set_run_request || changingRunRequest" @change="setRunRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Run</span></label>
              <small v-if="!capabilities.set_run_request">{{ controlAvailability('set_run_request') }}</small>
              <p v-if="runResult" class="action-result inline-action-result">{{ runResult }}</p>
            </div>
          </div>
          <small class="context-note">Named requests only; the STM32 remains the safety authority.</small>
        </div>
        <div class="overview-section battery-overview">
          <div class="primary-metrics">
            <div class="primary-metric"><span>Pack</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.pack_voltage_uV).toFixed(2)} V`) : '—' }}</b></div>
            <div class="primary-metric" :class="{ unavailable: !status?.current_sensing_enabled }"><span>Current</span><b>{{ currentValue }}</b><small>{{ status?.current_sensing_enabled ? 'Positive is charging' : 'Development configuration' }}</small></div>
            <div class="primary-metric" :class="{ unavailable: !status?.soc_valid }"><span>SoC</span><b>{{ socValue }}</b><small>Last calibration: {{ socCalibration }}</small></div>
          </div>
          <div class="hv-metrics">
            <div class="primary-metric" :class="{ unavailable: !snapshot?.hv_voltages?.valid }"><span>BAT+ · AMC3330</span><b>{{ hvVoltage('bat_plus_uV') }}</b></div>
            <div class="primary-metric" :class="{ unavailable: !snapshot?.hv_voltages?.valid }"><span>LOAD+ · AMC3330</span><b>{{ hvVoltage('load_plus_uV') }}</b></div>
          </div>
          <div class="cell-summary"><span>Cells</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.min_cell_uV).toFixed(3)}–${cellVoltageV(snapshot.pack.max_cell_uV).toFixed(3)} V`) : '—' }}</b><small>{{ snapshot ? measurement(`Δ ${cellDeltaMv.toFixed(1)} mV`) : '—' }} · {{ snapshot ? measurement(`NTC ${ntcCelsius(snapshot.pack.min_ntc_raw).toFixed(1)}–${ntcCelsius(snapshot.pack.max_ntc_raw).toFixed(1)} °C`) : '—' }} · {{ snapshot ? measurement(`IC ${icCelsius(snapshot.pack.min_ic_raw).toFixed(1)}–${icCelsius(snapshot.pack.max_ic_raw).toFixed(1)} °C`) : '—' }}</small></div>
        </div>
      </section>
      <section class="panel attention">
        <div class="panel-heading"><div><h2>Status</h2></div><p v-if="status && attentionItems.length">{{ liveIssueCount ? `${liveIssueCount} live issue${liveIssueCount === 1 ? '' : 's'}` : '' }}{{ liveIssueCount && (pendingIssueCount || warningCount) ? ' · ' : '' }}{{ pendingIssueCount ? `${pendingIssueCount} acknowledgement pending` : '' }}{{ pendingIssueCount && warningCount ? ' · ' : '' }}{{ warningCount ? `${warningCount} warning${warningCount === 1 ? '' : 's'}` : '' }}</p></div>
        <p v-if="!status" class="attention-waiting">Waiting for BMS status.</p>
        <div v-else-if="attentionItems.length === 0" class="attention-ok"><span aria-hidden="true">✓</span><div><strong>System OK</strong><small>No active errors, acknowledgement pending, or warnings.</small></div></div>
        <div v-else class="attention-list"><article v-for="item in attentionItems" :key="`${item.domain}-${item.label}`" class="attention-item" :data-state="item.state"><span class="attention-icon" aria-hidden="true">{{ item.state === 'warning' ? '▲' : '!' }}</span><div><strong>{{ item.label }}</strong><small>{{ item.state === 'live' ? 'Live error' : item.state === 'pending' ? 'Acknowledgement pending' : 'Warning' }} · {{ item.domain }}</small></div></article></div>
      </section>
      <section class="panel activity">
        <div class="panel-heading"><div><h2>Activity</h2></div><p>This Companion session</p></div>
        <p v-if="recentEvents.length === 0" class="muted">No changes this session.</p>
        <template v-else>
          <RecentChangesList :events="recentEvents" :limit="3" />
          <div class="activity-actions"><button @click="emit('showAll')">Show all</button></div>
        </template>
      </section>
    </section>

    <section class="panel"><div class="panel-heading cell-panel-heading"><div><h2>Cell voltages and balancing</h2></div><div class="context-control"><p v-if="!fresh">Values remain hidden until a complete fresh snapshot arrives.</p></div></div><div v-if="fresh && snapshot" class="table-scroll"><table><thead><tr><th>Slave</th><th v-for="index in 12" :key="index">C{{ index }}</th></tr></thead><tbody><tr v-for="cell in snapshot.cells" :key="cell.slave_index"><th>Slave {{ cell.slave_index }}</th><td v-for="(value, index) in cell.cell_voltage_uV" :key="index" :class="{ balancing: (cell.balance_mask & (1 << index)) !== 0 }">{{ cellVoltageV(value).toFixed(3) }} V<span v-if="(cell.balance_mask & (1 << index)) !== 0"> balancing</span></td></tr></tbody></table></div></section>

    <section class="panel"><div class="panel-heading"><div><h2>Temperatures</h2></div></div><div v-if="fresh && snapshot" class="table-scroll"><table><thead><tr><th>Slave</th><th>NTC 0</th><th>NTC 1</th><th>NTC 2</th><th>NTC 3</th><th>IC</th></tr></thead><tbody><tr v-for="temperature in snapshot.temperatures" :key="temperature.slave_index"><th>Slave {{ temperature.slave_index }}</th><td v-for="(value, index) in temperature.ntc_raw" :key="index">{{ ntcCelsius(value).toFixed(1) }} °C</td><td>{{ icCelsius(temperature.ic_temp_raw).toFixed(1) }} °C</td></tr></tbody></table></div></section>

    <section class="panel logging-panel"><div class="panel-heading"><div><h2>CSV logging</h2></div><p>No data is sent to the Gateway or stored on the BMS.</p></div><div class="button-row"><button class="primary" :disabled="!fresh || logging" @click="startLogging">Start logging</button><button :disabled="!logging" @click="stopLogging">Stop logging</button><button :disabled="lines.length < 2" @click="downloadCsv(lines)">Download {{ Math.max(0, lines.length - 1) }} rows</button></div></section>

    <ServiceView :transport="transport" :capabilities="capabilities" :connected="connected" :acknowledgement-pending="acknowledgementPending" :stm32-uptime-ms="status?.uptime_ms" :gateway="gateway" />
  </main>
</template>
