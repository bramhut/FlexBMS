<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import type { BmsTransport, Capabilities, RuntimeConfiguration, ServiceResponse } from '@/transports/Transport'
import { serviceResultLabel } from '@/shared/service'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean }>()

type EditableConfiguration = Omit<RuntimeConfiguration, 'reason' | 'expected_version' | 'stored_version' | 'battery_capacity_mah'> & {
  battery_capacity_ah: number
}

const configuration = reactive<EditableConfiguration>({
  slave_count: 1,
  current_sense_slave: 1,
  shunt_resistance_uohm: 10000,
  battery_capacity_ah: 314,
  invert_current: false,
  balance_enabled: true,
})
const configurationReason = ref<RuntimeConfiguration['reason']>('blank')
const configurationExpectedVersion = ref<number | null>(null)
const configurationStoredVersion = ref<number | null>(null)
const configurationError = ref('')
const configurationBusy = ref(false)
const result = ref('')

const configurationStatus = computed(() => {
  if (configurationReason.value === 'valid') return `Active configuration (version ${configurationExpectedVersion.value ?? 'unknown'}).`
  if (configurationReason.value === 'version_mismatch') return `NO_CONFIG: stored version ${configurationStoredVersion.value ?? 'unknown'} is not supported by this firmware (expected ${configurationExpectedVersion.value ?? 'unknown'}).`
  if (configurationReason.value === 'corrupt') return 'NO_CONFIG: the stored configuration is corrupt. Factory defaults are shown.'
  return 'NO_CONFIG: no configuration has been saved to this board. Factory defaults are shown.'
})

function describe(response: ServiceResponse): string {
  return serviceResultLabel(response.result)
}

async function refreshConfiguration(showFailure = true): Promise<void> {
  if (!props.connected || !props.capabilities.runtime_configuration) return
  configurationError.value = ''
  const response = await props.transport.request('get_config', {})
  const data = response.data
  if (response.result !== 'ok' || data?.reason === undefined || data.expected_version === undefined || data.slave_count === undefined ||
      data.current_sense_slave === undefined || data.shunt_resistance_uohm === undefined || data.battery_capacity_mah === undefined || data.invert_current === undefined || data.balance_enabled === undefined) {
    configurationError.value = describe(response)
    if (showFailure) result.value = `Configuration read: ${describe(response)}`
    return
  }
  configurationReason.value = data.reason
  configurationExpectedVersion.value = data.expected_version
  configurationStoredVersion.value = data.stored_version ?? 0
  configuration.slave_count = data.slave_count
  configuration.current_sense_slave = data.current_sense_slave
  configuration.shunt_resistance_uohm = data.shunt_resistance_uohm
  configuration.battery_capacity_ah = data.battery_capacity_mah / 1000
  configuration.invert_current = data.invert_current
  configuration.balance_enabled = data.balance_enabled
}

function capacityMilliAh(): number | null {
  if (!Number.isFinite(configuration.battery_capacity_ah)) return null
  const milliAh = Math.round(configuration.battery_capacity_ah * 1000)
  return Math.abs(configuration.battery_capacity_ah * 1000 - milliAh) < 1e-6 ? milliAh : null
}

function validateConfiguration(): string {
  if (!Number.isInteger(configuration.slave_count) || configuration.slave_count < 1 || configuration.slave_count > 32) return 'Slave count must be between 1 and 32.'
  if (!Number.isInteger(configuration.current_sense_slave) || configuration.current_sense_slave < 0 || configuration.current_sense_slave > configuration.slave_count) return 'Current sensing must be none or one of the configured slaves.'
  if (!Number.isInteger(configuration.shunt_resistance_uohm) || configuration.shunt_resistance_uohm < 1 || configuration.shunt_resistance_uohm > 1_000_000) return 'Shunt resistance must be between 1 and 1,000,000 micro-ohms.'
  const milliAh = capacityMilliAh()
  if (milliAh === null || milliAh < 1 || milliAh > 10_000_000) return 'Battery capacity must be between 0.001 and 10,000 Ah, with at most three decimal places.'
  return ''
}

async function saveConfiguration(): Promise<void> {
  configurationError.value = validateConfiguration()
  const milliAh = capacityMilliAh()
  if (configurationError.value || milliAh === null) return
  configurationBusy.value = true
  try {
    const response = await props.transport.request('set_config', {
      slave_count: configuration.slave_count,
      current_sense_slave: configuration.current_sense_slave,
      shunt_resistance_uohm: configuration.shunt_resistance_uohm,
      battery_capacity_mah: milliAh,
      invert_current: configuration.invert_current,
      balance_enabled: configuration.balance_enabled,
    })
    result.value = `Configuration save: ${describe(response)}`
    if (response.result === 'ok') configurationReason.value = 'valid'
  } finally {
    configurationBusy.value = false
  }
}

watch([() => props.connected, () => props.capabilities.runtime_configuration], ([connected, supported]) => {
  if (connected && supported) void refreshConfiguration(false)
}, { immediate: true })
</script>

<template>
  <main class="stack configuration-page">
    <section class="panel">
      <div class="panel-heading"><div><h2>Runtime configuration</h2></div><p>Stored in STM32 FLASH and applied after reboot. Safety limits remain firmware-controlled.</p></div>
      <p :class="configurationReason === 'valid' ? 'muted' : 'warning-text'">{{ configurationStatus }}</p>
      <div class="configuration-form">
        <label class="configuration-field"><span>Slave count</span><input v-model.number="configuration.slave_count" type="number" min="1" max="32" step="1" inputmode="numeric"><small>Number of BCC slaves in the chain (1–32).</small></label>
        <label class="configuration-field"><span>Current sensing slave</span><select v-model.number="configuration.current_sense_slave"><option :value="0">None</option><option v-for="index in configuration.slave_count" :key="index" :value="index">Slave {{ index }}</option></select><small>Choose one slave, or disable current sensing.</small></label>
        <label class="configuration-field"><span>Shunt resistance (µΩ)</span><input v-model.number="configuration.shunt_resistance_uohm" type="number" min="1" max="1000000" step="1" inputmode="numeric"><small>Enter the measured shunt value in micro-ohms.</small></label>
        <label class="configuration-field"><span>Battery capacity (Ah)</span><input v-model.number="configuration.battery_capacity_ah" type="number" min="0.001" max="10000" step="0.001" inputmode="decimal"><small>Precision: 0.001 Ah (1 mAh).</small></label>
        <label class="configuration-checkbox"><input v-model="configuration.invert_current" type="checkbox"><span>Invert current direction</span><small>Use only if the installed current sensor reports the opposite sign.</small></label>
        <label class="configuration-checkbox"><input v-model="configuration.balance_enabled" type="checkbox" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy"><span>Automatic cell balancing</span><small>Saved with the configuration and applied after reboot.</small></label>
      </div>
      <p v-if="configurationError" class="warning-text">{{ configurationError }}</p>
      <div class="button-row configuration-actions">
        <button :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @click="refreshConfiguration()">Read configuration</button>
        <button class="primary" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @click="saveConfiguration()">Save and reboot</button>
      </div>
      <p v-if="result" class="action-result">{{ result }}</p>
      <small v-if="!capabilities.runtime_configuration">Runtime configuration is unavailable in the current Gateway state.</small>
    </section>
  </main>
</template>
