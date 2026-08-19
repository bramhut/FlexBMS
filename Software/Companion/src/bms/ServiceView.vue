<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { BmsTransport, Capabilities, GatewayStatus, ServiceResponse } from '@/transports/Transport'
import { registerFields } from '@/shared/registers'
import { serviceResultLabel } from '@/shared/service'
import { advancingUnixTime, advancingUptimeMs, formatUptime } from '@/shared/time'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; runRequested: boolean; balancingRequested: boolean; acknowledgementPending: boolean; stm32UptimeMs?: number; gateway?: GatewayStatus }>()
const requested = ref(false)
const changingRunRequest = ref(false)
const balancingRequested = ref(false)
const changingBalancingRequest = ref(false)
const slave = ref(0)
const registerText = ref('03')
const result = ref('')
const registerValue = ref<number | null>(null)
const registerError = ref('')
const deviceUnixTime = ref<number | null>(null)
const deviceTimeSampledAt = ref(0)
const deviceTimeError = ref('')
const clockNow = ref(Date.now())
const stm32UptimeSampledAt = ref(0)
const gatewayUptimeSampledAt = ref(0)
const previousStm32UptimeMs = ref<number>()
const previousGatewayUptimeMs = ref<number>()
const stm32Restarted = ref(false)
const gatewayRestarted = ref(false)
let clockTimer: number | undefined

const availability = (capability: keyof Capabilities) => props.capabilities[capability] ? '' : 'Unavailable in the current Gateway state.'
const registerKey = computed(() => registerText.value.trim().toUpperCase().padStart(2, '0'))
const fields = computed(() => registerFields[registerKey.value] ?? [])
const bits = computed(() => registerValue.value === null ? '' : registerValue.value.toString(2).padStart(16, '0'))
const formattedDeviceTime = computed(() => {
  if (deviceUnixTime.value === null) {
    if (!props.connected) return 'Connect to read device time.'
    return deviceTimeError.value ? `Unavailable (${deviceTimeError.value})` : 'Reading device time…'
  }
  const unixTime = advancingUnixTime(deviceUnixTime.value, deviceTimeSampledAt.value, clockNow.value)
  return new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'medium' }).format(new Date(unixTime * 1000))
})
const formattedNtpSync = computed(() => {
  const timeSync = props.gateway?.time_sync
  if (!timeSync) return 'Gateway only.'
  if (timeSync.state === 'synchronized' && timeSync.last_sync_unix_s !== undefined) {
    return new Intl.DateTimeFormat(undefined, { dateStyle: 'medium', timeStyle: 'medium' }).format(new Date(timeSync.last_sync_unix_s * 1000))
  }
  return ({ waiting_for_network: 'Waiting for network.', waiting_for_ntp: 'Waiting for NTP.', waiting_for_stm32: 'Updating STM32.', synchronized: 'Waiting for NTP.' } as const)[timeSync.state]
})
const formattedStm32Uptime = computed(() => {
  if (!props.connected) return 'Not connected.'
  if (props.stm32UptimeMs === undefined) return 'Waiting for BMS status.'
  return formatUptime(advancingUptimeMs(props.stm32UptimeMs, stm32UptimeSampledAt.value, clockNow.value))
})
const formattedGatewayUptime = computed(() => {
  if (!props.gateway) return 'Gateway only.'
  if (!props.connected) return 'Not connected.'
  const uptimeMs = props.gateway?.gateway_uptime_ms
  if (uptimeMs === undefined) return 'Waiting for Gateway status.'
  return formatUptime(advancingUptimeMs(uptimeMs, gatewayUptimeSampledAt.value, clockNow.value))
})

function describe(response: ServiceResponse): string {
  if (response.data?.value !== undefined) return `${serviceResultLabel(response.result)}: 0x${response.data.value.toString(16).padStart(4, '0').toUpperCase()}`
  return serviceResultLabel(response.result)
}

async function setRunRequest(): Promise<void> {
  const requestedValue = requested.value
  changingRunRequest.value = true
  const response = await props.transport.request('set_run_request', { requested: requestedValue })
  result.value = `Run request: ${describe(response)}`
  changingRunRequest.value = false
  if (response.result !== 'ok') requested.value = props.runRequested
}

async function setBalancingRequest(): Promise<void> {
  const requestedValue = balancingRequested.value
  changingBalancingRequest.value = true
  const response = await props.transport.request('set_balancing_request', { requested: requestedValue })
  result.value = `Balancing request: ${describe(response)}`
  changingBalancingRequest.value = false
  if (response.result !== 'ok') balancingRequested.value = props.balancingRequested
}

async function acknowledgeFaults(): Promise<void> {
  const response = await props.transport.request('acknowledge_faults', {})
  result.value = `Fault acknowledgement: ${describe(response)}`
}

async function refreshDeviceTime(showFailure = true): Promise<void> {
  if (!props.connected || !props.capabilities.get_rtc) return
  const response = await props.transport.request('get_rtc', {})
  if (response.result === 'ok' && response.data?.unix_time_s !== undefined) {
    deviceUnixTime.value = response.data.unix_time_s
    deviceTimeSampledAt.value = Date.now()
    deviceTimeError.value = ''
    return
  }
  deviceTimeError.value = describe(response)
  if (showFailure) result.value = `RTC read: ${describe(response)}`
}

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

watch(() => props.runRequested, value => { if (!changingRunRequest.value) requested.value = value }, { immediate: true })
watch(() => props.balancingRequested, value => { if (!changingBalancingRequest.value) balancingRequested.value = value }, { immediate: true })
watch([() => props.connected, () => props.capabilities.get_rtc], ([connected, supported]) => { if (connected && supported) void refreshDeviceTime(false) }, { immediate: true })
watch(() => props.stm32UptimeMs, uptimeMs => {
  if (uptimeMs === undefined) return
  if (previousStm32UptimeMs.value !== undefined && uptimeMs < previousStm32UptimeMs.value &&
      !(previousStm32UptimeMs.value > 0xf0000000 && uptimeMs < 0x0fffffff)) stm32Restarted.value = true
  previousStm32UptimeMs.value = uptimeMs
  stm32UptimeSampledAt.value = Date.now()
}, { immediate: true })
watch(() => props.gateway?.gateway_uptime_ms, uptimeMs => {
  if (uptimeMs === undefined) return
  if (previousGatewayUptimeMs.value !== undefined && uptimeMs < previousGatewayUptimeMs.value) gatewayRestarted.value = true
  previousGatewayUptimeMs.value = uptimeMs
  gatewayUptimeSampledAt.value = Date.now()
}, { immediate: true })
clockTimer = window.setInterval(() => { clockNow.value = Date.now() }, 1000)
onBeforeUnmount(() => { if (clockTimer !== undefined) window.clearInterval(clockTimer) })
</script>

<template>
  <section class="panel controls-panel">
    <div class="panel-heading"><div><h2>BMS controls</h2></div><p>Named requests only; the STM32 remains the safety authority.</p></div>
    <div class="control-layout">
      <div class="control-section">
        <h3>Requests</h3>
        <div class="switch-row"><label class="ios-switch"><input v-model="requested" type="checkbox" role="switch" :disabled="!capabilities.set_run_request || changingRunRequest" @change="setRunRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Request BMS run</span></label><small v-if="!capabilities.set_run_request">{{ availability('set_run_request') }}</small></div>
        <div class="switch-row"><label class="ios-switch"><input v-model="balancingRequested" type="checkbox" role="switch" :disabled="!capabilities.set_balancing_request || changingBalancingRequest" @change="setBalancingRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Request cell balancing</span></label><small v-if="!capabilities.set_balancing_request">{{ availability('set_balancing_request') }}</small></div>
      </div>
      <div v-if="acknowledgementPending" class="control-section recovery-section">
        <h3>Error recovery</h3>
        <p>Acknowledgement clears resolved error latches and a recorded watchdog reset. Live and configuration warnings remain until their condition changes.</p>
        <button class="danger" :disabled="!capabilities.acknowledge_faults" @click="acknowledgeFaults">Acknowledge resolved faults</button>
        <small v-if="!capabilities.acknowledge_faults">{{ availability('acknowledge_faults') }}</small>
      </div>
    </div>
    <p v-if="result" class="action-result">{{ result }}</p>
  </section>

  <section class="panel clock-panel">
    <div class="panel-heading"><div><h2>System time</h2></div><p>UTC is stored on the STM32; dates are shown in this browser’s locale.</p></div>
    <dl class="clock-details"><div><dt>Device time</dt><dd>{{ formattedDeviceTime }}</dd></div><div><dt>Last NTP sync</dt><dd>{{ formattedNtpSync }}</dd></div><div><dt>BMS uptime</dt><dd>{{ formattedStm32Uptime }}</dd><small v-if="stm32Restarted">Restart detected in this Companion session.</small></div><div><dt>Gateway uptime</dt><dd>{{ formattedGatewayUptime }}</dd><small v-if="gatewayRestarted">Restart detected in this Companion session.</small></div></dl>
    <button :disabled="!connected || !capabilities.get_rtc" @click="refreshDeviceTime()">Refresh device time</button>
    <small v-if="!capabilities.get_rtc">{{ availability('get_rtc') }}</small>
  </section>

  <section class="panel register-panel">
    <div class="panel-heading"><div><h2>Read BCC register</h2></div><p>Read-only. Slave indexes are zero-based.</p></div>
    <div class="register-form"><label>Slave<input v-model.number="slave" type="number" min="0" max="255"></label><label>Register (hex)<input v-model="registerText" maxlength="2" inputmode="text"></label><button :disabled="!capabilities.read_register" @click="readRegister">Read register</button></div>
    <p v-if="registerError" class="warning-text">{{ registerError }}</p>
    <div v-if="registerValue !== null" class="register-result"><p><b>0x{{ registerKey }}</b> = <b>0x{{ registerValue.toString(16).padStart(4, '0').toUpperCase() }}</b></p><p class="mono">{{ bits }}</p><ul v-if="fields.length"><li v-for="field in fields" :key="field.name">{{ field.name }} ({{ field.bits }} bit{{ field.bits === 1 ? '' : 's' }})</li></ul><p v-else class="muted">No compact field description is available for this address.</p></div>
    <small>{{ availability('read_register') }}</small>
  </section>
</template>
