<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import type { BmsTransport, Capabilities, GatewayStatus, RuntimeConfiguration, ServiceResponse, WifiNetwork } from '@/transports/Transport'
import { serviceResultLabel } from '@/shared/service'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()

type EditableConfiguration = Omit<RuntimeConfiguration, 'reason' | 'expected_version' | 'stored_version' | 'battery_capacity_mah'> & {
  battery_capacity_ah: number
}

const configuration = reactive<EditableConfiguration>({
  slave_count: 1,
  current_sense_slave: 1,
  shunt_resistance_uohm: 375,
  battery_capacity_ah: 314,
  invert_current: false,
  balance_enabled: true,
  startup_diagnostics: true,
})
const configurationReason = ref<RuntimeConfiguration['reason']>('blank')
const configurationExpectedVersion = ref<number | null>(null)
const configurationStoredVersion = ref<number | null>(null)
const configurationError = ref('')
const configurationBusy = ref(false)
const result = ref('')
const networks = ref<WifiNetwork[]>([])
const wifiSsid = ref('')
const wifiPassword = ref('')
const wifiResult = ref('')
const wifiScanning = ref(false)
const wifiSaving = ref(false)
const mqttHost = ref('')
const mqttPort = ref(1883)
const mqttUsername = ref('')
const mqttPassword = ref('')
const mqttResult = ref('')
const mqttSaving = ref(false)

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
      data.current_sense_slave === undefined || data.shunt_resistance_uohm === undefined || data.battery_capacity_mah === undefined || data.invert_current === undefined || data.balance_enabled === undefined || data.startup_diagnostics === undefined) {
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
  configuration.startup_diagnostics = data.startup_diagnostics
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
      startup_diagnostics: configuration.startup_diagnostics,
    })
    result.value = `Configuration save: ${describe(response)}`
    if (response.result === 'ok') configurationReason.value = 'valid'
  } finally {
    configurationBusy.value = false
  }
}

function confirmStartupDiagnosticsChange(event: Event): void {
  const input = event.target as HTMLInputElement
  if (input.checked || window.confirm('Startup diagnostics are intended to detect BCC and hardware problems during startup. Disabling them is only meant for debugging or bench testing. Continue?')) return
  configuration.startup_diagnostics = true
}

function selectNetwork(network: WifiNetwork): void { wifiSsid.value = network.ssid }

async function scanWifi(): Promise<void> {
  if (!props.capabilities.wifi_configuration || wifiScanning.value) return
  wifiScanning.value = true
  wifiResult.value = ''
  const response = await props.transport.scanWifi()
  wifiScanning.value = false
  if (response.result === 'ok') {
    networks.value = response.networks ?? []
    wifiResult.value = networks.value.length === 0 ? 'No nearby networks found.' : 'Select a network or enter a hidden SSID manually.'
    return
  }
  wifiResult.value = response.result === 'rate_limited' ? 'Please wait before scanning again.' : `Scan unavailable: ${response.result}.`
}

async function saveWifi(): Promise<void> {
  if (!props.capabilities.wifi_configuration || wifiSaving.value) return
  if (wifiSsid.value.length === 0 || wifiSsid.value.length > 32 || wifiPassword.value.length > 63) {
    wifiResult.value = 'SSID must be 1–32 bytes and password at most 63 bytes.'
    return
  }
  wifiSaving.value = true
  const response = await props.transport.configureWifi(wifiSsid.value, wifiPassword.value)
  wifiSaving.value = false
  wifiResult.value = response.result === 'accepted'
    ? 'Saved. The Gateway is restarting Wi-Fi; reconnect on the new network or its recovery AP if needed.'
    : `Could not save Wi-Fi settings: ${response.result}.`
}

async function saveMqtt(): Promise<void> {
  if (!props.capabilities.mqtt_configuration || mqttSaving.value) return
  if (mqttHost.value.length === 0 || mqttHost.value.length > 63 || mqttUsername.value.length === 0 || mqttUsername.value.length > 63 || mqttPassword.value.length === 0 || mqttPassword.value.length > 63 || mqttPort.value < 1 || mqttPort.value > 65535) {
    mqttResult.value = 'Enter a broker host, port 1–65535, username, and password (each at most 63 bytes).'
    return
  }
  mqttSaving.value = true
  const response = await props.transport.configureMqtt(mqttHost.value, mqttPort.value, mqttUsername.value, mqttPassword.value)
  mqttSaving.value = false
  mqttResult.value = response.result === 'accepted' ? 'Saved. The Gateway is connecting to MQTT on the station LAN.' : `Could not save MQTT settings: ${response.result}.`
  if (response.result === 'accepted') mqttPassword.value = ''
}

watch(() => props.gateway?.mqtt, mqtt => {
  if (!mqtt) return
  mqttHost.value = mqtt.host ?? ''
  mqttPort.value = mqtt.port ?? 1883
  mqttUsername.value = mqtt.username ?? ''
}, { immediate: true })

watch([() => props.connected, () => props.capabilities.runtime_configuration], ([connected, supported]) => {
  if (connected && supported) void refreshConfiguration(false)
}, { immediate: true })
</script>

<template>
  <main class="stack configuration-page">
    <section class="panel">
      <div class="panel-heading"><div><h2>BMS</h2></div><p>Stored in STM32 FLASH and applied after reboot. Safety limits remain firmware-controlled.</p></div>
      <p :class="configurationReason === 'valid' ? 'muted' : 'warning-text'">{{ configurationStatus }}</p>
      <div class="configuration-form">
        <label class="configuration-field"><span>Slave count</span><input v-model.number="configuration.slave_count" type="number" min="1" max="32" step="1" inputmode="numeric"><small>Number of BCC slaves in the chain (1–32).</small></label>
        <label class="configuration-field"><span>Current sensing slave</span><select v-model.number="configuration.current_sense_slave"><option :value="0">None</option><option v-for="index in configuration.slave_count" :key="index" :value="index">Slave {{ index }}</option></select><small>Choose one slave, or disable current sensing.</small></label>
        <label class="configuration-field"><span>Shunt resistance (µΩ)</span><input v-model.number="configuration.shunt_resistance_uohm" type="number" min="1" max="1000000" step="1" inputmode="numeric"><small>Enter the measured shunt value in micro-ohms.</small></label>
        <label class="configuration-field"><span>Battery capacity (Ah)</span><input v-model.number="configuration.battery_capacity_ah" type="number" min="0.001" max="10000" step="0.001" inputmode="decimal"><small>Precision: 0.001 Ah (1 mAh).</small></label>
        <label class="configuration-checkbox"><input v-model="configuration.invert_current" type="checkbox"><span>Invert current direction</span><small>Use only if the installed current sensor reports the opposite sign.</small></label>
        <label class="configuration-checkbox"><input v-model="configuration.balance_enabled" type="checkbox" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy"><span>Automatic cell balancing</span><small>Saved with the configuration and applied after reboot.</small></label>
        <label class="configuration-checkbox dangerous-configuration"><input v-model="configuration.startup_diagnostics" type="checkbox" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @change="confirmStartupDiagnosticsChange"><span>Run startup diagnostics</span><small>Dangerous: disabling this is only for debugging or bench testing. It reduces startup checks for BCC and hardware problems.</small></label>
      </div>
      <p v-if="configurationError" class="warning-text">{{ configurationError }}</p>
      <div class="button-row configuration-actions">
        <button :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @click="refreshConfiguration()">Read configuration</button>
        <button class="primary" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @click="saveConfiguration()">Save and reboot</button>
      </div>
      <p v-if="result" class="action-result">{{ result }}</p>
      <small v-if="!capabilities.runtime_configuration">Runtime configuration is unavailable in the current Gateway state.</small>
    </section>

    <section class="panel networking-panel">
      <div class="panel-heading"><div><h2>Networking</h2></div><p>Configure the Gateway network connection and Home Assistant MQTT integration.</p></div>
      <div class="configuration-subsection">
        <div class="subsection-heading"><h3>Wi-Fi</h3><p>State: {{ gateway?.wifi_state ?? 'unavailable' }}<span v-if="gateway?.wifi_ssid"> · {{ gateway.wifi_ssid }}</span></p></div>
        <p v-if="gateway?.setup_ap.active" class="muted">Setup AP {{ gateway.setup_ap.ssid }} is active. Open http://{{ gateway.setup_ap.address }}.</p>
        <p v-if="gateway?.setup_ap.active" class="muted">The setup AP is open temporarily. Monitoring and Wi-Fi setup are available there; BMS service controls are disabled.</p>
        <div class="button-row"><button :disabled="!capabilities.wifi_configuration || wifiScanning" @click="scanWifi">{{ wifiScanning ? 'Scanning…' : 'Scan nearby networks' }}</button></div>
        <ul v-if="networks.length" class="network-list">
          <li v-for="network in networks" :key="network.ssid"><button @click="selectNetwork(network)">{{ network.ssid || '(hidden network)' }}</button><span> · {{ network.rssi }} dBm · {{ network.secure ? 'secured' : 'open' }}</span></li>
        </ul>
        <div class="configuration-form">
          <label class="configuration-field"><span>Wi-Fi name (SSID)</span><input v-model="wifiSsid" maxlength="32" autocomplete="off" required><small>Enter a visible or hidden network name.</small></label>
          <label class="configuration-field"><span>Password</span><input v-model="wifiPassword" maxlength="63" type="password" autocomplete="new-password"><small>Passwords are write-only and are not shown after saving.</small></label>
        </div>
        <div class="button-row configuration-actions"><button :disabled="!capabilities.wifi_configuration || wifiSaving" @click="saveWifi">{{ wifiSaving ? 'Saving…' : 'Save and reconnect' }}</button></div>
        <p v-if="wifiResult" class="action-result">{{ wifiResult }}</p>
      </div>

      <div class="configuration-subsection">
        <div class="subsection-heading"><h3>Home Assistant MQTT</h3><p>State: {{ gateway?.mqtt_state ?? 'unavailable' }}<span v-if="gateway?.mqtt.configured"> · {{ gateway.mqtt.host }}:{{ gateway.mqtt.port }} · {{ gateway.mqtt.username }}</span></p></div>
        <p v-if="gateway?.setup_ap.active" class="muted">MQTT credentials can only be changed from the trusted station LAN, never through the open setup AP.</p>
        <template v-else>
          <div class="configuration-form">
            <label class="configuration-field"><span>Broker host or IP</span><input v-model="mqttHost" maxlength="63" autocomplete="off" required><small>Address of the local MQTT broker.</small></label>
            <label class="configuration-field"><span>Broker port</span><input v-model.number="mqttPort" type="number" min="1" max="65535" required><small>Use a port from 1 to 65535.</small></label>
            <label class="configuration-field"><span>Broker username</span><input v-model="mqttUsername" maxlength="63" autocomplete="off" required><small>MQTT credentials are write-only.</small></label>
            <label class="configuration-field"><span>Broker password</span><input v-model="mqttPassword" maxlength="63" type="password" autocomplete="new-password" required><small>Passwords are not displayed after saving.</small></label>
          </div>
          <div class="button-row configuration-actions"><button :disabled="!capabilities.mqtt_configuration || mqttSaving" @click="saveMqtt">{{ mqttSaving ? 'Saving…' : 'Save MQTT settings' }}</button></div>
          <small>Credentials are write-only. This v1 local-LAN connection uses normal MQTT over TCP; do not expose the Gateway or broker to the Internet.</small>
          <p v-if="mqttResult" class="action-result">{{ mqttResult }}</p>
        </template>
      </div>
    </section>
  </main>
</template>
