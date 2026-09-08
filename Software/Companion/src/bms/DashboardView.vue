<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import ServiceView from './ServiceView.vue'
import RecentChangesList from './RecentChangesList.vue'
import { balancingDisplay, bccDiagnosticNames, bccDiagnosticStatusNames, bmsFaultNames, bmsStateName, cellVoltageV, currentA, describeWatchdogBreadcrumb, formatCellDifferenceMv, formatEnergy, formatPower, hvReasonNames, hvStateName, icCelsius, ntcCelsius, powerW, setBits, socPercent, warningDisplayNames } from '@/shared/model'
import { serviceResultLabel } from '@/shared/service'
import { advancingUnixTime, createUptimeTracker, displayedUptimeMs, formatUptime, resetUptimeTracker, sampleUptime } from '@/shared/time'
import type { BccDiagnosticReport, BmsTransport, Capabilities, GatewayStatus, RecordedControllerEvent, ServiceResponse, Snapshot, Status } from '@/transports/Transport'

const props = defineProps<{ snapshot: Snapshot | null; status: Status | null; transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus; recentEvents: RecordedControllerEvent[]; diagnosticReports: BccDiagnosticReport[]; deviceTimeUnixS: number | null; deviceTimeSampledAt: number; bmsUptimeSampledAt: number; gatewayUptimeSampledAt: number; heatmapEnabled: boolean; deltaViewEnabled: boolean }>()
const emit = defineEmits<{
  (event: 'showAll'): void
  (event: 'update:heatmapEnabled', value: boolean): void
  (event: 'update:deltaViewEnabled', value: boolean): void
}>()
const requested = ref(false)
const changingRunRequest = ref(false)
const runError = ref('')
let runErrorTimer: number | undefined
const clockNow = ref(Date.now())
const stm32Uptime = createUptimeTracker()
const gatewayUptime = createUptimeTracker()
const stm32DisplayedUptimeMs = ref<number>()
const gatewayDisplayedUptimeMs = ref<number>()
const stm32Restarted = ref(false)
const gatewayRestarted = ref(false)
const formattedDeviceTime = computed(() => {
  if (props.deviceTimeUnixS === null) return props.connected ? 'Waiting for device time.' : 'Connect to read device time.'
  const unixTime = advancingUnixTime(props.deviceTimeUnixS, props.deviceTimeSampledAt, clockNow.value)
  return new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'medium' }).format(new Date(unixTime * 1000))
})
const formattedStm32Uptime = computed(() => {
  if (!props.connected) return 'Not connected.'
  if (stm32DisplayedUptimeMs.value === undefined) return 'Waiting for BMS status.'
  return formatUptime(stm32DisplayedUptimeMs.value)
})
const formattedGatewayUptime = computed(() => {
  if (!props.gateway) return 'Gateway only.'
  if (!props.connected) return 'Not connected.'
  if (gatewayDisplayedUptimeMs.value === undefined) return 'Waiting for Gateway status.'
  return formatUptime(gatewayDisplayedUptimeMs.value)
})
const fresh = computed(() => Boolean(props.snapshot && props.status?.measurements_fresh))
const measurement = (value: string) => fresh.value ? value : '—'
type AttentionItem = { label: string; domain: 'BMS' | 'HV' | 'System'; state: 'live' | 'pending' | 'warning'; detail?: string }

const friendlyName = (name: string) => name.toLowerCase().replace(/_/g, ' ').replace(/\b[a-z]/g, (letter: string) => letter.toUpperCase())
const diagnosticReportSummary = computed(() => {
  if (props.diagnosticReports.length === 0) return 'Detailed report unavailable; open Diagnostics after the BMS reconnects.'
  return props.diagnosticReports.map(report => {
    const checks = bccDiagnosticNames.filter((_, bit) => (report.failed_checks & (1 << bit)) !== 0)
    const status = report.status_code === 0 ? '' : `${bccDiagnosticStatusNames[report.status_code] ?? `BCC status ${report.status_code}`} during ${bccDiagnosticNames[report.failed_diagnostic] ?? 'diagnostic execution'}`
    const detail = [...checks, status].filter(Boolean).join('; ')
    return `Slave ${report.slave_index + 1}: ${detail || 'no failed check recorded'}`
  }).join(' · ')
})
const attentionItems = computed<AttentionItem[]>(() => {
  const status = props.status
  if (!status) return []
  const items: AttentionItem[] = []
  const addErrors = (active: number, latched: number, names: string[], domain: 'BMS' | 'HV') => {
    names.forEach((name, bit) => {
      const bitMask = 1 << bit
      if ((active & bitMask) !== 0) items.push({ label: friendlyName(name), domain, state: 'live', ...(name === 'BCC_DIAGNOSTICS' ? { detail: diagnosticReportSummary.value } : {}) })
      else if ((latched & bitMask) !== 0) items.push({ label: friendlyName(name), domain, state: 'pending', ...(name === 'BCC_DIAGNOSTICS' ? { detail: diagnosticReportSummary.value } : {}) })
    })
  }
  addErrors(status.bms_active_errors, status.bms_latched_errors, bmsFaultNames, 'BMS')
  addErrors(status.hv_active_errors, status.hv_latched_errors, hvReasonNames, 'HV')
  setBits(status.warnings, warningDisplayNames).forEach(name => items.push({
    label: name,
    domain: 'System',
    state: 'warning',
    ...(name === 'Watchdog reset' ? { detail: describeWatchdogBreadcrumb(status.watchdog_breadcrumb, status.watchdog_diagnostic) ?? 'Warning · System' } : {}),
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
type CellReading = { slaveIndex: number; cellIndex: number; voltageUv: number }
const cellReadings = computed<CellReading[]>(() => props.snapshot?.cells.flatMap(cell => cell.cell_voltage_uV.map((voltageUv, cellIndex) => ({ slaveIndex: cell.slave_index, cellIndex, voltageUv }))) ?? [])
const cellOverview = computed(() => {
  if (cellReadings.value.length === 0) return null
  const minimum = cellReadings.value.reduce((current, reading) => reading.voltageUv < current.voltageUv ? reading : current)
  const maximumVoltageUv = Math.max(...cellReadings.value.map(reading => reading.voltageUv))
  return { minimum, maximumVoltageUv, spreadUv: maximumVoltageUv - minimum.voltageUv }
})
const heatmapDeadbandUv = 5_000
const toggleHeatmap = () => emit('update:heatmapEnabled', !props.heatmapEnabled)
const toggleDeltaView = () => emit('update:deltaViewEnabled', !props.deltaViewEnabled)
const cellLabel = (slaveIndex: number, cellIndex: number) => {
  const cellNumber = String(cellIndex + 1)
  return props.snapshot && props.snapshot.cells.length > 1 ? `S${slaveIndex + 1} C${cellNumber}` : `C${cellNumber}`
}
const cellDifferenceUv = (voltageUv: number) => Math.max(0, voltageUv - (cellOverview.value?.minimum.voltageUv ?? voltageUv))
const cellDifference = (voltageUv: number) => formatCellDifferenceMv(cellDifferenceUv(voltageUv))
const cellDisplayValue = (voltageUv: number) => props.deltaViewEnabled ? `${cellDifference(voltageUv)} mV` : `${cellVoltageV(voltageUv).toFixed(3)} V`
const cellHeatmapStyle = (voltageUv: number) => {
  const overview = cellOverview.value
  if (!props.heatmapEnabled || !overview || overview.spreadUv <= heatmapDeadbandUv) return undefined

  const heatmapRangeUv = overview.spreadUv - heatmapDeadbandUv
  const normalized = Math.min(1, Math.max(0, (cellDifferenceUv(voltageUv) - heatmapDeadbandUv) / heatmapRangeUv))
  if (normalized === 0) return undefined

  const hue = Math.round(145 * (1 - normalized))
  const saturation = Math.round(55 + normalized * 15)
  const lightness = Math.round(96 - normalized * 5)
  return { backgroundColor: `hsl(${hue} ${saturation}% ${lightness}%)` }
}
const cellReference = computed(() => {
  const overview = cellOverview.value
  if (!overview) return null
  return {
    label: cellLabel(overview.minimum.slaveIndex, overview.minimum.cellIndex),
    voltage: `${cellVoltageV(overview.minimum.voltageUv).toFixed(3)} V`,
    spread: `${formatCellDifferenceMv(overview.spreadUv)} mV`,
  }
})
const cellDeltaMv = computed(() => props.snapshot ? (props.snapshot.pack.max_cell_uV - props.snapshot.pack.min_cell_uV) / 1000 : 0)
const currentValue = computed(() => {
  if (!props.status?.current_sensing_enabled) return 'Disabled'
  return props.snapshot ? measurement(`${currentA(props.snapshot.pack.pack_current_raw).toFixed(2)} A`) : '—'
})
const powerValue = computed(() => {
  if (!props.status?.current_sensing_enabled) return 'Disabled'
  return props.snapshot ? measurement(formatPower(powerW(props.snapshot.pack.pack_voltage_uV, props.snapshot.pack.pack_current_raw))) : '—'
})
const hvDisplay = computed(() => {
  const status = props.status
  if (!status) return { detail: 'Waiting', ready: false }

  const state = hvStateName(status.hv_state)
  if (status.hv_state !== 0) return { detail: state, ready: false }

  const ready = (status.flags & (1 << 0)) !== 0
  return { detail: `${state} · ${ready ? 'Ready' : 'Not ready'}`, ready }
})
const balancing = computed(() => balancingDisplay(props.status, props.snapshot?.cells ?? null))
const socValue = computed(() => props.snapshot && props.status?.soc_valid ? measurement(`${socPercent(props.snapshot.pack.soc_raw).toFixed(1)} %`) : 'Unavailable')
const chargedEnergyValue = computed(() => props.snapshot?.energy ? measurement(formatEnergy(props.snapshot.energy.charged_energy_uWh, props.snapshot.energy.valid)) : 'Unavailable')
const dischargedEnergyValue = computed(() => props.snapshot?.energy ? measurement(formatEnergy(props.snapshot.energy.discharged_energy_uWh, props.snapshot.energy.valid)) : 'Unavailable')
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
function clearRunError(): void {
  runError.value = ''
  if (runErrorTimer !== undefined) {
    window.clearTimeout(runErrorTimer)
    runErrorTimer = undefined
  }
}
function showRunError(response: ServiceResponse): void {
  if (response.result === 'ok') return
  runError.value = `Run request: ${describeControlResponse(response)}`
  runErrorTimer = window.setTimeout(clearRunError, 5000)
}
async function setRunRequest(): Promise<void> {
  const requestedValue = requested.value
  clearRunError()
  changingRunRequest.value = true
  const response = await props.transport.request('set_run_request', { requested: requestedValue })
  showRunError(response)
  changingRunRequest.value = false
  if (response.result !== 'ok') requested.value = props.status?.run_request ?? false
}
watch(() => props.status?.run_request, value => { if (!changingRunRequest.value) requested.value = value ?? false }, { immediate: true })
watch([() => props.status?.uptime_ms, () => props.bmsUptimeSampledAt], ([uptimeMs, sampledAt]) => {
  if (uptimeMs === undefined || sampledAt === 0) return
  const result = sampleUptime(stm32Uptime, uptimeMs, sampledAt)
  if (result === 'restart') stm32Restarted.value = true
  if (result !== 'stale') stm32DisplayedUptimeMs.value = displayedUptimeMs(stm32Uptime)
}, { immediate: true })
watch([() => props.gateway?.gateway_uptime_ms, () => props.gatewayUptimeSampledAt], ([uptimeMs, sampledAt]) => {
  if (uptimeMs === undefined || sampledAt === 0) return
  const result = sampleUptime(gatewayUptime, uptimeMs, sampledAt)
  if (result === 'restart') gatewayRestarted.value = true
  if (result !== 'stale') gatewayDisplayedUptimeMs.value = displayedUptimeMs(gatewayUptime)
}, { immediate: true })
watch(() => props.connected, connected => {
  if (connected) return
  resetUptimeTracker(stm32Uptime)
  resetUptimeTracker(gatewayUptime)
  stm32DisplayedUptimeMs.value = undefined
  gatewayDisplayedUptimeMs.value = undefined
}, { immediate: true })
watch(() => props.gateway?.uart_state, state => {
  if (state !== 'lost' && state !== 'starting') return
  resetUptimeTracker(stm32Uptime)
  stm32DisplayedUptimeMs.value = undefined
})
const uptimeTimer = window.setInterval(() => {
  stm32DisplayedUptimeMs.value = displayedUptimeMs(stm32Uptime)
  gatewayDisplayedUptimeMs.value = displayedUptimeMs(gatewayUptime)
}, 250)
const clockTimer = window.setInterval(() => { clockNow.value = Date.now() }, 1000)
onBeforeUnmount(() => {
  window.clearInterval(uptimeTimer)
  window.clearInterval(clockTimer)
  clearRunError()
})
</script>

<template>
  <main class="dashboard">
    <section class="device-status-bar" aria-label="Device status">
      <strong class="device-status-label">Device status</strong>
      <dl class="clock-details"><div><dt>Device time</dt><dd>{{ formattedDeviceTime }}</dd></div><div><dt>BMS uptime</dt><dd>{{ formattedStm32Uptime }}</dd><small v-if="stm32Restarted">Restart detected in this Companion session.</small></div><div><dt>Gateway uptime</dt><dd>{{ formattedGatewayUptime }}</dd><small v-if="gatewayRestarted">Restart detected in this Companion session.</small></div></dl>
      <small class="device-status-note">Updated every minute</small>
    </section>
    <section class="dashboard-top">
      <section class="panel overview">
        <div class="overview-section">
          <span class="section-label">Operating</span>
          <div class="operating-row">
            <div class="state-chips">
              <span class="state-chip"><b>BMS</b>{{ status ? bmsStateName(status.bms_state) : 'Waiting' }}</span>
              <span class="state-chip" :class="{ ready: hvDisplay.ready }"><b>HV</b>{{ hvDisplay.detail }}</span>
              <span class="state-chip" :data-state="balancing.state"><b>Balancing</b>{{ balancing.label }}</span>
            </div>
            <div class="context-control">
              <label class="ios-switch"><input v-model="requested" type="checkbox" role="switch" :disabled="!capabilities.set_run_request || changingRunRequest" @change="setRunRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Run</span></label>
              <small v-if="!capabilities.set_run_request">{{ controlAvailability('set_run_request') }}</small>
            </div>
          </div>
          <p v-if="runError" class="action-warning" role="alert">{{ runError }}</p>
          <small class="context-note">Named requests only; the STM32 remains the safety authority.</small>
        </div>
        <div class="overview-section battery-overview">
          <div class="primary-metrics">
            <div class="primary-metric"><span>Pack</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.pack_voltage_uV).toFixed(2)} V`) : '—' }}</b></div>
            <div class="primary-metric" :class="{ unavailable: !status?.current_sensing_enabled }"><span>Current</span><b>{{ currentValue }}</b><small>{{ status?.current_sensing_enabled ? 'Positive is charging' : 'Development configuration' }}</small></div>
            <div class="primary-metric" :class="{ unavailable: !status?.current_sensing_enabled }"><span>Power</span><b>{{ powerValue }}</b></div>
          </div>
          <div class="energy-metrics">
            <div class="primary-metric" :class="{ unavailable: !status?.soc_valid }"><span>SoC</span><b>{{ socValue }}</b><small>Last calibration: {{ socCalibration }}</small></div>
            <div class="primary-metric" :class="{ unavailable: !snapshot?.energy?.valid }"><span>Energy charged</span><b>{{ chargedEnergyValue }}</b><small>Persistent total</small></div>
            <div class="primary-metric" :class="{ unavailable: !snapshot?.energy?.valid }"><span>Energy discharged</span><b>{{ dischargedEnergyValue }}</b><small>Persistent total</small></div>
          </div>
          <div class="cell-summary"><span>Cells</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.min_cell_uV).toFixed(3)}–${cellVoltageV(snapshot.pack.max_cell_uV).toFixed(3)} V`) : '—' }}</b><small>{{ snapshot ? measurement(`Δ ${cellDeltaMv.toFixed(1)} mV`) : '—' }} · {{ snapshot ? measurement(`NTC ${ntcCelsius(snapshot.pack.min_ntc_raw).toFixed(1)}–${ntcCelsius(snapshot.pack.max_ntc_raw).toFixed(1)} °C`) : '—' }} · {{ snapshot ? measurement(`IC ${icCelsius(snapshot.pack.min_ic_raw).toFixed(1)}–${icCelsius(snapshot.pack.max_ic_raw).toFixed(1)} °C`) : '—' }}</small></div>
        </div>
      </section>
      <section class="panel attention">
        <div class="panel-heading"><div><h2>Status</h2></div><p v-if="status && attentionItems.length">{{ liveIssueCount ? `${liveIssueCount} live issue${liveIssueCount === 1 ? '' : 's'}` : '' }}{{ liveIssueCount && (pendingIssueCount || warningCount) ? ' · ' : '' }}{{ pendingIssueCount ? `${pendingIssueCount} acknowledgement pending` : '' }}{{ pendingIssueCount && warningCount ? ' · ' : '' }}{{ warningCount ? `${warningCount} warning${warningCount === 1 ? '' : 's'}` : '' }}</p></div>
        <p v-if="!status" class="attention-waiting">Waiting for BMS status.</p>
        <div v-else-if="attentionItems.length === 0" class="attention-ok"><span aria-hidden="true">✓</span><div><strong>System OK</strong><small>No active errors, acknowledgement pending, or warnings.</small></div></div>
        <div v-else class="attention-list"><article v-for="item in attentionItems" :key="`${item.domain}-${item.label}`" class="attention-item" :data-state="item.state"><span class="attention-icon" aria-hidden="true">{{ item.state === 'warning' ? '▲' : '!' }}</span><div><strong>{{ item.label }}</strong><small>{{ item.detail ?? `${item.state === 'live' ? 'Live error' : item.state === 'pending' ? 'Acknowledgement pending' : 'Warning'} · ${item.domain}` }}</small></div></article></div>
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

    <section class="panel">
      <div class="panel-heading cell-panel-heading">
        <div><h2>Cell voltage offsets</h2><p>{{ props.deltaViewEnabled ? 'Positive offset from lowest cell · mV' : 'Absolute cell voltage · V' }}</p></div>
        <div class="cell-panel-actions"><span class="balancing-legend"><svg class="balancing-chevron header-balancing-chevron" viewBox="0 0 20 20" role="img" aria-label="Balancing active"><path d="M3 4.5 10 11.5 17 4.5" /><path d="M3 10.5 10 17.5 17 10.5" /></svg><span>Balancing active</span></span><button type="button" class="table-view-toggle" :aria-pressed="props.deltaViewEnabled" :title="props.deltaViewEnabled ? 'Switch to absolute cell voltages' : 'Switch to voltage offsets'" @click="toggleDeltaView">{{ props.deltaViewEnabled ? 'Δ view' : 'V view' }}</button><button type="button" class="heatmap-toggle" :aria-pressed="props.heatmapEnabled" @click="toggleHeatmap">Heatmap</button><p v-if="!fresh">Values remain hidden until a complete fresh snapshot arrives.</p></div>
      </div>
      <div v-if="fresh && snapshot" class="table-scroll"><table><thead><tr class="cell-table-summary-row"><th colspan="13"><span class="cell-table-summary-metric"><span class="cell-table-summary-label">Lowest cell</span><span class="cell-table-summary-value">{{ cellReference ? `${cellReference.label} · ${cellReference.voltage}` : '—' }}</span></span><span class="cell-table-summary-metric"><span class="cell-table-summary-label">Spread</span><span class="cell-table-summary-secondary">{{ cellReference?.spread ?? '—' }}</span></span></th></tr><tr><th>Slave</th><th v-for="index in 12" :key="index">C{{ index }}</th></tr></thead><tbody><tr v-for="cell in snapshot.cells" :key="cell.slave_index"><th>Slave {{ cell.slave_index + 1 }}</th><td v-for="(value, index) in cell.cell_voltage_uV" :key="index" :style="cellHeatmapStyle(value)" :title="`${cellLabel(cell.slave_index, index)} · ${cellVoltageV(value).toFixed(3)} V`"><span class="cell-difference-content" :class="{ 'delta-view': props.deltaViewEnabled }"><span class="balancing-marker-slot"><svg v-if="(cell.balance_mask & (1 << index)) !== 0" class="balancing-chevron" viewBox="0 0 20 20" role="img" aria-label="Balancing active"><path d="M3 4.5 10 11.5 17 4.5" /><path d="M3 10.5 10 17.5 17 10.5" /></svg></span><span class="cell-difference-value">{{ cellDisplayValue(value) }}</span></span></td></tr></tbody></table></div>
    </section>

    <section class="panel"><div class="panel-heading"><div><h2>Temperatures</h2></div></div><div v-if="fresh && snapshot" class="table-scroll"><table class="temperature-table"><thead><tr><th>Slave</th><th>NTC 0</th><th>NTC 1</th><th>NTC 2</th><th>NTC 3</th><th>IC</th></tr></thead><tbody><tr v-for="temperature in snapshot.temperatures" :key="temperature.slave_index"><th>Slave {{ temperature.slave_index }}</th><td v-for="(value, index) in temperature.ntc_raw" :key="index">{{ ntcCelsius(value).toFixed(1) }} °C</td><td>{{ icCelsius(temperature.ic_temp_raw).toFixed(1) }} °C</td></tr></tbody></table></div></section>

    <ServiceView :transport="transport" :capabilities="capabilities" :connected="connected" :acknowledgement-pending="acknowledgementPending" />
  </main>
</template>
