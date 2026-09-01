<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import ConnectionHeader from './ConnectionHeader.vue'
import RecentChangesDrawer from '@/bms/RecentChangesDrawer.vue'
import DashboardView from '@/bms/DashboardView.vue'
import DiagnosticsView from '@/bms/DiagnosticsView.vue'
import FirmwareView from '@/bms/FirmwareView.vue'
import ConfigurationView from '@/bms/ConfigurationView.vue'
import { GatewayTransport } from '@/transports/GatewayTransport'
import { WebSerialTransport } from '@/transports/WebSerialTransport'
import type { BccDiagnosticReport, BmsTransport, ConnectionState, GatewayStatus, RecordedControllerEvent, Snapshot, Status } from '@/transports/Transport'
const gatewayTarget = __FLEXBMS_TARGET__ === 'gateway'
const transport: BmsTransport = gatewayTarget ? new GatewayTransport() : new WebSerialTransport()
const active = ref<'dashboard' | 'diagnostics' | 'configuration' | 'firmware'>('dashboard'); const state = ref<ConnectionState>('disconnected'); const snapshot = ref<Snapshot | null>(null); const bmsStatus = ref<Status | null>(null); const gateway = ref<GatewayStatus>(); const recentEvents = ref<RecordedControllerEvent[]>([]); const recentChangesOpen = ref(false)
const deviceTimeUnixS = ref<number | null>(null)
const deviceTimeSampledAt = ref(0)
let deviceTimePollTimer: number | undefined
let deviceTimeRequestInFlight = false
const capabilities = ref(transport.getCapabilities()); const fresh = computed(() => Boolean(bmsStatus.value?.measurements_fresh) && (!gateway.value || gateway.value.uart_state === 'healthy'))
const diagnosticReports = ref<BccDiagnosticReport[]>([])
let diagnosticReportRequestInFlight = false
let lastDiagnosticFaultMask = 0
const bccDiagnosticsMask = 1 << 2
const connectionTone = computed<'healthy' | 'waiting' | 'problem'>(() => {
  if (state.value === 'disconnected' || gateway.value?.uart_state === 'lost' || gateway.value?.uart_state === 'starting') return 'problem'
  return fresh.value ? 'healthy' : 'waiting'
})
const connectionMessage = computed(() => {
  if (state.value === 'disconnected') return 'Not connected'
  if (state.value === 'connecting') return 'Connecting'
  if (gateway.value && gateway.value.uart_state !== 'healthy') return `STM32 UART ${gateway.value.uart_state}`
  return fresh.value ? 'Connected · telemetry healthy' : 'Connected · waiting for fresh telemetry'
})
async function pollDeviceTime(): Promise<void> {
  if (state.value !== 'connected' || !capabilities.value.get_rtc || deviceTimeRequestInFlight) return
  deviceTimeRequestInFlight = true
  try {
    const response = await transport.request('get_rtc', {})
    if (response.result === 'ok' && response.data?.unix_time_s !== undefined) {
      deviceTimeUnixS.value = response.data.unix_time_s
      deviceTimeSampledAt.value = Date.now()
    }
  } finally {
    deviceTimeRequestInFlight = false
  }
}

function startDeviceTimePolling(): void {
  if (deviceTimePollTimer !== undefined) return
  void pollDeviceTime()
  deviceTimePollTimer = window.setInterval(() => { void pollDeviceTime() }, 60_000)
}

function stopDeviceTimePolling(): void {
  if (deviceTimePollTimer !== undefined) {
    window.clearInterval(deviceTimePollTimer)
    deviceTimePollTimer = undefined
  }
}

async function refreshDiagnosticReports(status: Status): Promise<void> {
  if (state.value !== 'connected' || !capabilities.value.get_diagnostic_report || diagnosticReportRequestInFlight) return
  diagnosticReportRequestInFlight = true
  try {
    const reports: BccDiagnosticReport[] = []
    for (let slaveIndex = 0; slaveIndex < status.slave_count; slaveIndex += 1) {
      const response = await transport.request('get_diagnostic_report', { slave_index: slaveIndex })
      const data = response.data
      if (response.result === 'ok' && data?.slave_index !== undefined && data.cid !== undefined && data.failed_checks !== undefined && data.status_code !== undefined && data.failed_diagnostic !== undefined) {
        reports.push({ slave_index: data.slave_index, cid: data.cid, failed_checks: data.failed_checks, status_code: data.status_code, failed_diagnostic: data.failed_diagnostic })
      }
    }
    if (reports.length > 0) diagnosticReports.value = reports
  } finally {
    diagnosticReportRequestInFlight = false
  }
}

const offBmsStatus = transport.onBmsStatus(next => {
  bmsStatus.value = next
  const diagnosticFaultMask = (next.bms_active_errors | next.bms_latched_errors) & bccDiagnosticsMask
  if (diagnosticFaultMask !== 0 && diagnosticFaultMask !== lastDiagnosticFaultMask) {
    lastDiagnosticFaultMask = diagnosticFaultMask
    void refreshDiagnosticReports(next)
  }
}); const offSnapshot = transport.onSnapshot(next => { snapshot.value = next; bmsStatus.value = next.status }); const offEvent = transport.onEvent(event => { recentEvents.value = [{ ...event, observed_at_ms: Date.now() }, ...recentEvents.value].slice(0, 12) }); const offState = transport.onState((next, gatewayStatus) => { state.value = next; capabilities.value = transport.getCapabilities(); if (gatewayStatus) gateway.value = gatewayStatus; if (next !== 'connected') { stopDeviceTimePolling(); diagnosticReports.value = []; lastDiagnosticFaultMask = 0 } else startDeviceTimePolling() })
async function connect(): Promise<void> { try { await transport.connect(); capabilities.value = transport.getCapabilities() } catch (error) { console.error('Companion connection failed', error) } }
onMounted(() => { if (gatewayTarget) void connect() })
onBeforeUnmount(() => { offBmsStatus(); offSnapshot(); offEvent(); offState(); stopDeviceTimePolling(); transport.disconnect() })
</script>
<template>
  <ConnectionHeader :label="transport.label" :message="connectionMessage" :tone="connectionTone" :allow-manual-connect="!gatewayTarget && state === 'disconnected'" @connect="connect" />
  <nav>
    <button :class="{ active: active === 'dashboard' }" @click="active = 'dashboard'">BMS dashboard</button>
    <button :class="{ active: active === 'diagnostics' }" @click="active = 'diagnostics'; recentChangesOpen = false">Diagnostics</button>
    <button v-if="capabilities.runtime_configuration" :class="{ active: active === 'configuration' }" @click="active = 'configuration'; recentChangesOpen = false">Configuration</button>
    <button :class="{ active: active === 'firmware' }" @click="active = 'firmware'; recentChangesOpen = false">Firmware</button>
  </nav>
  <DashboardView v-if="active === 'dashboard'" :snapshot="snapshot" :status="bmsStatus" :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" :recent-events="recentEvents" :diagnostic-reports="diagnosticReports" :device-time-unix-s="deviceTimeUnixS" :device-time-sampled-at="deviceTimeSampledAt" @show-all="recentChangesOpen = true" />
  <DiagnosticsView v-else-if="active === 'diagnostics'" :snapshot="snapshot" :status="bmsStatus" :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :diagnostic-reports="diagnosticReports" />
  <ConfigurationView v-else-if="active === 'configuration'" :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" />
  <FirmwareView v-else :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" />
  <RecentChangesDrawer :events="recentEvents" :open="recentChangesOpen" @close="recentChangesOpen = false" />
</template>
