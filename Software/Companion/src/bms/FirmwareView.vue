<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import FirmwareUpdatePanel from './FirmwareUpdatePanel.vue'
import { serviceResultLabel } from '@/shared/service'
import type { BmsTransport, Capabilities, GatewayStatus, ServiceResponse } from '@/transports/Transport'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()
const stm32Version = ref<number | null>(null)
const versionError = ref('')
const reading = ref(false)
const gatewayVersion = computed(() => {
  if (!props.connected) return 'Connect to read version.'
  const version = props.gateway?.gateway_version
  // The Gateway appends SemVer build metadata to detect a changed browser
  // bundle. It is transport identity, not part of the release shown to users.
  return version?.split('+', 1)[0] ?? 'Unavailable on a direct USB connection'
})
const canReadStm32Version = computed(() => props.connected && props.capabilities.get_device_info && (props.gateway === undefined || props.gateway.uart_state === 'healthy'))
const stm32VersionText = computed(() => {
  if (!props.connected) return 'Connect to read version.'
  if (props.gateway !== undefined && props.gateway.uart_state !== 'healthy') return `Unavailable (STM32 UART ${props.gateway.uart_state})`
  if (stm32Version.value === null) return versionError.value || 'Reading…'
  const packed = stm32Version.value
  const version = `${packed & 0xff}.${(packed >>> 8) & 0xff}.${(packed >>> 16) & 0xff}`
  const build = (packed >>> 24) & 0xff
  return build === 0 ? version : `${version} (build ${build})`
})

function describe(response: ServiceResponse): string { return serviceResultLabel(response.result) }
async function refreshVersions(): Promise<void> {
  if (!canReadStm32Version.value || reading.value) return
  reading.value = true
  versionError.value = ''
  try {
    const response = await props.transport.request('get_device_info', {})
    if (response.result === 'ok' && response.data?.firmware_version_packed !== undefined) stm32Version.value = response.data.firmware_version_packed
    else versionError.value = `Unavailable (${describe(response)})`
  } catch { versionError.value = 'Unavailable (no response)' }
  finally { reading.value = false }
}

watch([() => props.connected, () => props.capabilities.get_device_info, () => props.gateway?.uart_state], () => {
  if (canReadStm32Version.value) void refreshVersions()
}, { immediate: true })
</script>

<template>
  <main class="stack">
    <section class="panel">
      <div class="panel-heading"><div><h2>Firmware versions</h2></div><p>Versions are read from the connected controllers.</p></div>
      <dl class="firmware-versions">
        <div><dt>ESP32 Gateway</dt><dd>{{ gatewayVersion }}</dd></div>
        <div><dt>STM32 BMS</dt><dd>{{ stm32VersionText }}</dd></div>
      </dl>
      <template v-if="canReadStm32Version">
      <button :disabled="!connected || !capabilities.get_device_info || reading" @click="refreshVersions">{{ reading ? 'Reading…' : 'Refresh versions' }}</button>
      </template>
      <button v-else disabled>Refresh versions</button>
    </section>
    <FirmwareUpdatePanel :capabilities="capabilities" :connected="connected" :gateway="gateway" />
  </main>
</template>
