<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { BmsTransport, Capabilities, GatewayStatus, ServiceResponse } from '@/transports/Transport'
import { registerFields } from '@/shared/registers'
import { serviceResultLabel } from '@/shared/service'
import { advancingUnixTime } from '@/shared/time'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; runRequested: boolean; balancingRequested: boolean; gateway?: GatewayStatus }>()
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
clockTimer = window.setInterval(() => { clockNow.value = Date.now() }, 1000)
onBeforeUnmount(() => { if (clockTimer !== undefined) window.clearInterval(clockTimer) })
</script>

<template>
  <section class="panel controls-panel">
    <div class="panel-heading"><div><h2>BMS controls</h2></div><p>Named requests only; the STM32 remains the safety authority.</p></div>
    <div class="control-grid">
      <div class="control-block"><h3>Run request</h3><label class="ios-switch"><input v-model="requested" type="checkbox" role="switch" :disabled="!capabilities.set_run_request || changingRunRequest" @change="setRunRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Request BMS run</span></label><small>{{ availability('set_run_request') }}</small></div>
      <div class="control-block"><h3>Cell balancing</h3><label class="ios-switch"><input v-model="balancingRequested" type="checkbox" role="switch" :disabled="!capabilities.set_balancing_request || changingBalancingRequest" @change="setBalancingRequest"><span class="ios-switch-track" aria-hidden="true"><span class="ios-switch-thumb"></span></span><span>Request balancing</span></label><small>Development default: off. The STM32 still requires all balancing safety conditions.</small><small>{{ availability('set_balancing_request') }}</small></div>
      <div class="control-block"><h3>Fault handling</h3><p>Acknowledgement clears inactive error latches; the STM32 remains the safety authority.</p><button class="danger" :disabled="!capabilities.acknowledge_faults" @click="acknowledgeFaults">Acknowledge errors</button><small>{{ availability('acknowledge_faults') }}</small></div>
      <div class="control-block"><h3>Real-time clock</h3><p>Device time: <b>{{ formattedDeviceTime }}</b></p><p>Last NTP sync: <b>{{ formattedNtpSync }}</b></p><div class="button-row"><button :disabled="!connected || !capabilities.get_rtc" @click="refreshDeviceTime()">Refresh</button></div><small>{{ availability('get_rtc') }}</small></div>
    </div>
    <p v-if="result" class="action-result">{{ result }}</p>
  </section>

  <section class="panel register-panel">
    <div class="panel-heading"><div><h2>Read BCC register</h2></div><p>Read-only. Slave indexes are zero-based.</p></div>
    <div class="register-form"><label>Slave<input v-model.number="slave" type="number" min="0" max="255"></label><label>Register (hex)<input v-model="registerText" maxlength="2" inputmode="text"></label><button :disabled="!capabilities.read_register" @click="readRegister">Read register</button></div>
    <p v-if="registerError" class="warning-text">{{ registerError }}</p>
    <div v-if="registerValue !== null" class="register-result"><p><b>0x{{ registerKey }}</b> = <b>0x{{ registerValue.toString(16).padStart(4, '0').toUpperCase() }}</b></p><p class="mono">{{ bits }}</p><ul v-if="fields.length"><li v-for="field in fields" :key="field.name">{{ field.name }} ({{ field.bits }} bit{{ field.bits === 1 ? '' : 's' }})</li></ul><p v-else class="muted">No compact field description is available for this address.</p></div>
    <small>{{ availability('read_register') }}</small>
  </section>
</template>
