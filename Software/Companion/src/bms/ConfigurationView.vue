<script setup lang="ts">
import EnergyMetersConfiguration from './EnergyMetersConfiguration.vue'
import { computed, reactive, ref, watch } from 'vue'
import type { BmsTransport, Capabilities, GatewayStatus, RuntimeConfiguration, ServiceResponse, WifiNetwork } from '@/transports/Transport'
import { serviceResultLabel } from '@/shared/service'
import { selfDischargeEquivalentMilliAmps } from '@/shared/model'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()

type EditableConfiguration = Omit<RuntimeConfiguration, 'reason' | 'expected_version' | 'stored_version' | 'battery_capacity_mah' | 'self_discharge_tenth_percent_per_30_days'> & {
  battery_capacity_ah: number
  self_discharge_percent: number
}

const configuration = reactive<EditableConfiguration>({
  slave_count: 1,
  current_sense_slave: 1,
  shunt_resistance_uohm: 375,
  battery_capacity_ah: 314,
  invert_current: false,
  balance_enabled: true,
  startup_diagnostics: true,
  self_discharge_percent: 0,
  cold_allowance_deci_c: 30,
})
const configurationReason = ref<RuntimeConfiguration['reason'] | null>(null)
const coldAllowanceC = computed({
  get: () => configuration.cold_allowance_deci_c / 10,
  set: (value: number) => {
    const deciC = value * 10
    const rounded = Math.round(deciC)
    configuration.cold_allowance_deci_c = Math.abs(deciC - rounded) < 1e-6 ? rounded : deciC
  },
})
const configurationLoaded = ref(false)
const configurationReading = ref(false)
let configurationConnectionGeneration = 0
const configurationExpectedVersion = ref<number | null>(null)
const configurationStoredVersion = ref<number | null>(null)
const configurationError = ref('')
const configurationBusy = ref(false)
const savedConfiguration = ref<EditableConfiguration | null>(null)
const configurationDirty = computed(() => savedConfiguration.value !== null &&
  (Object.keys(configuration) as Array<keyof EditableConfiguration>).some(key => configuration[key] !== savedConfiguration.value?.[key]))
const configurationEditable = computed(() => props.connected && props.capabilities.runtime_configuration && configurationLoaded.value && !configurationReading.value && !configurationBusy.value)
const configurationCanSave = computed(() => configurationEditable.value && (configurationDirty.value || configurationReason.value !== 'valid'))
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
const wifiEditing = ref(false)
const mqttEditing = ref(false)
const wifiFormVisible = computed(() => wifiEditing.value || Boolean(props.gateway?.setup_ap.active))
const networkStateLabel = (state: string | undefined) => !props.connected ? 'Disconnected' : ({ connected: 'Connected', connecting: 'Connecting', provisioning: 'Setup needed', recovery: 'Recovery', lost: 'Disconnected', unavailable: 'Unavailable' }[state ?? 'unavailable'] ?? state)

const wifiDisconnectReason = computed(() => {
  if (!props.gateway?.wifi_last_disconnect_reason_valid) return null
  return props.gateway.wifi_last_disconnect_reason ?? null
})

const configurationStatus = computed(() => {
  if (!configurationLoaded.value) return configurationReading.value ? 'Reading configuration…' : 'Configuration not loaded. Read the board configuration before saving.'
  if (configurationReason.value === 'valid') return `Active configuration (version ${configurationExpectedVersion.value ?? 'unknown'}).`
  if (configurationReason.value === 'version_mismatch') return `NO_CONFIG: stored version ${configurationStoredVersion.value ?? 'unknown'} is not supported by this firmware (expected ${configurationExpectedVersion.value ?? 'unknown'}).`
  if (configurationReason.value === 'corrupt') return 'NO_CONFIG: the stored configuration is corrupt. Factory defaults are shown.'
  return 'NO_CONFIG: no configuration has been saved to this board. Factory defaults are shown.'
})

function describe(response: ServiceResponse): string {
  return serviceResultLabel(response.result)
}

async function refreshConfiguration(showFailure = true): Promise<void> {
  if (!props.connected || !props.capabilities.runtime_configuration || configurationReading.value || configurationBusy.value) return
  configurationReading.value = true
  const generation = configurationConnectionGeneration
  configurationLoaded.value = false
  configurationError.value = ''
  try {
    const response = await props.transport.request('get_config', {})
    if (generation !== configurationConnectionGeneration || !props.connected) return
    const data = response.data
    if (response.result !== 'ok' || data?.reason === undefined || data.expected_version === undefined || data.slave_count === undefined ||
        data.current_sense_slave === undefined || data.shunt_resistance_uohm === undefined || data.battery_capacity_mah === undefined || data.invert_current === undefined || data.balance_enabled === undefined || data.startup_diagnostics === undefined) {
      configurationError.value = response.result === 'ok' ? 'Invalid configuration response' : describe(response)
      if (showFailure) result.value = `Configuration read: ${configurationError.value}`
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
    configuration.self_discharge_percent = (data.self_discharge_tenth_percent_per_30_days ?? 0) / 10
    configuration.cold_allowance_deci_c = data.cold_allowance_deci_c ?? 30
    savedConfiguration.value = { ...configuration }
    configurationLoaded.value = true
  } catch {
    configurationError.value = 'Configuration read failed'
  } finally {
    configurationReading.value = false
    if (generation !== configurationConnectionGeneration && props.connected && props.capabilities.runtime_configuration) void refreshConfiguration(false)
  }
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
  if (!Number.isFinite(configuration.self_discharge_percent) || configuration.self_discharge_percent < 0 || configuration.self_discharge_percent > 5 || Math.abs(configuration.self_discharge_percent * 10 - Math.round(configuration.self_discharge_percent * 10)) > 1e-6) return 'Estimated self-discharge must be from 0 to 5% per 30 days in 0.1% steps.'
  if (!Number.isInteger(configuration.cold_allowance_deci_c) || configuration.cold_allowance_deci_c < 0 || configuration.cold_allowance_deci_c > 100) return 'Cold-temperature allowance must be from 0 to 10°C in 0.1°C steps.'
  return ''
}

async function saveConfiguration(): Promise<void> {
  if (!configurationCanSave.value) return
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
      self_discharge_tenth_percent_per_30_days: Math.round(configuration.self_discharge_percent * 10),
      cold_allowance_deci_c: configuration.cold_allowance_deci_c,
    })
    result.value = `Configuration save: ${describe(response)}`
    if (response.result === 'ok') {
      configurationReason.value = 'valid'
      savedConfiguration.value = { ...configuration }
    }
  } finally {
    configurationBusy.value = false
  }
}

function discardConfiguration(): void {
  if (savedConfiguration.value) Object.assign(configuration, savedConfiguration.value)
  configurationError.value = ''
  result.value = ''
}

function editWifi(): void {
  wifiSsid.value = props.gateway?.wifi_ssid ?? ''
  wifiPassword.value = ''
  wifiResult.value = ''
  wifiEditing.value = true
}
function editMqtt(): void {
  mqttHost.value = props.gateway?.mqtt.host ?? ''
  mqttPort.value = props.gateway?.mqtt.port ?? 1883
  mqttUsername.value = props.gateway?.mqtt.username ?? ''
  mqttPassword.value = ''
  mqttResult.value = ''
  mqttEditing.value = true
}
function cancelWifi(): void { wifiEditing.value = false; wifiPassword.value = ''; networks.value = []; wifiResult.value = '' }
function cancelMqtt(): void { mqttEditing.value = false; mqttPassword.value = ''; mqttResult.value = '' }

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
  if (response.result === 'accepted') { wifiPassword.value = ''; wifiEditing.value = false }
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
  if (response.result === 'accepted') { mqttPassword.value = ''; mqttEditing.value = false }
}

watch(() => props.gateway?.mqtt, mqtt => {
  if (!mqtt || mqttEditing.value) return
  mqttHost.value = mqtt.host ?? ''
  mqttPort.value = mqtt.port ?? 1883
  mqttUsername.value = mqtt.username ?? ''
}, { immediate: true })

watch([() => props.connected, () => props.capabilities.runtime_configuration], ([connected, supported]) => {
  ++configurationConnectionGeneration
  if (!connected || !supported) configurationLoaded.value = false
  if (connected && supported) void refreshConfiguration(false)
}, { immediate: true })
</script>

<template>
  <main class="stack configuration-page">
    <section class="panel">
      <div class="panel-heading"><div><h2>BMS configuration</h2><p>Saved to the BMS and applied after reboot. Safety limits remain firmware-controlled.</p></div><span v-if="configurationDirty" class="state-badge pending" role="status">Unsaved changes</span></div>
      <p :class="configurationReason === 'valid' ? 'muted' : 'warning-text'">{{ configurationStatus }}</p>
      <fieldset class="settings-fieldset" :disabled="!configurationEditable">
      <div class="bms-settings-grid">
        <div class="settings-group battery-settings"><h3>Battery</h3><div class="configuration-form">
        <label class="configuration-field"><span>Slave count</span><input v-model.number="configuration.slave_count" type="number" min="1" max="32" step="1" inputmode="numeric"><small>Number of BCC slaves in the chain (1–32).</small></label>
        <label class="configuration-field"><span>Battery capacity (Ah)</span><input v-model.number="configuration.battery_capacity_ah" type="number" min="0.001" max="10000" step="0.001" inputmode="decimal"><small>Precision: 0.001 Ah (1 mAh).</small></label>
        <label class="configuration-field"><span>Self-discharge (% / 30 days)</span><input v-model.number="configuration.self_discharge_percent" type="number" min="0" max="5" step="0.1" inputmode="decimal"><small>0 disables compensation. Equivalent current: {{ selfDischargeEquivalentMilliAmps(configuration.battery_capacity_ah, configuration.self_discharge_percent).toFixed(2) }} mA.</small></label>
        <label class="configuration-field"><span>Cold-temperature allowance (°C)</span><input v-model.number="coldAllowanceC" type="number" min="0" max="10" step="0.1" inputmode="decimal"><small>Subtracted from the lowest busbar temperature for cold-charge protection only. Default 3°C; not a sensor correction.</small></label>
        </div></div>
        <div class="settings-group"><h3>Current sensor</h3><div class="configuration-form">
        <label class="configuration-field"><span>Current sensing slave</span><select v-model.number="configuration.current_sense_slave"><option :value="0">None</option><option v-for="index in configuration.slave_count" :key="index" :value="index">Slave {{ index }}</option></select><small>Choose one slave, or disable current sensing.</small></label>
        <template v-if="configuration.current_sense_slave > 0">
        <label class="configuration-field"><span>Shunt resistance (µΩ)</span><input v-model.number="configuration.shunt_resistance_uohm" type="number" min="1" max="1000000" step="1" inputmode="numeric"><small>Enter the measured shunt value in micro-ohms.</small></label>
        <label class="configuration-checkbox"><input v-model="configuration.invert_current" type="checkbox"><span>Invert current direction</span><small>Use only if the installed current sensor reports the opposite sign.</small></label>
        </template>
        </div></div>
        <div class="settings-group operating-settings"><h3>Operating settings</h3>
        <label class="configuration-checkbox"><input v-model="configuration.balance_enabled" type="checkbox" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy"><span>Automatic cell balancing</span><small>Saved with the configuration and applied after reboot.</small></label>
        <label class="configuration-checkbox dangerous-configuration"><input v-model="configuration.startup_diagnostics" type="checkbox" :disabled="!connected || !capabilities.runtime_configuration || configurationBusy" @change="confirmStartupDiagnosticsChange"><span>Run startup diagnostics</span><small>Dangerous: disabling this is only for debugging or bench testing. It reduces startup checks for BCC and hardware problems.</small></label>
        </div>
      </div>
      </fieldset>
      <p v-if="configurationError" class="warning-text">{{ configurationError }}</p>
      <div class="button-row configuration-actions">
        <button class="primary" :disabled="!configurationCanSave" @click="saveConfiguration()">{{ configurationBusy ? 'Saving…' : 'Save and reboot' }}</button>
        <button :disabled="!configurationEditable || !configurationDirty" @click="discardConfiguration">Discard changes</button>
        <button :disabled="!connected || !capabilities.runtime_configuration || configurationBusy || configurationReading || configurationDirty" @click="refreshConfiguration()">{{ configurationReading ? 'Reading…' : 'Read configuration' }}</button>
      </div>
      <p v-if="result" class="action-result">{{ result }}</p>
      <small v-if="!capabilities.runtime_configuration">Runtime configuration is unavailable in the current Gateway state.</small>
    </section>

    <EnergyMetersConfiguration v-if="gateway?.energy_meters" :transport="transport" :connected="connected" :allowed="capabilities.energy_meters_configuration" :status="gateway.energy_meters" />

    <section class="panel networking-panel">
      <div class="panel-heading"><div><h2>Networking</h2><p>Gateway connection and Home Assistant integration.</p></div></div>
      <div class="networking-grid">
      <div class="network-card">
        <div class="subsection-heading"><h3>Wi-Fi</h3><span class="state-badge" :class="{ ready: connected && gateway?.wifi_state === 'connected' }">{{ networkStateLabel(gateway?.wifi_state) }}</span></div>
        <p class="network-name">{{ gateway?.wifi_ssid || 'No network configured' }}</p>
        <details class="technical-details"><summary>Connection details</summary>
        <p v-if="connected && gateway?.wifi_state === 'connected' && gateway.wifi_connection" class="muted">Access point: {{ gateway.wifi_connection.bssid }} · Channel {{ gateway.wifi_connection.channel }} · {{ gateway.wifi_connection.rssi_dbm }} dBm (last status sample)</p>
        <p v-if="wifiDisconnectReason !== null" class="muted">Last station disconnect reason: ESP-IDF code {{ wifiDisconnectReason }}.</p>
        <p v-if="!gateway?.wifi_connection && wifiDisconnectReason === null" class="muted">No connection details available.</p>
        </details>
        <p v-if="gateway?.setup_ap.active" class="muted">Setup AP {{ gateway.setup_ap.ssid }} is active. Open http://{{ gateway.setup_ap.address }}.</p>
        <p v-if="gateway?.setup_ap.active" class="muted">The setup AP is open temporarily. Monitoring and Wi-Fi setup are available there; BMS service controls are disabled.</p>
        <button v-if="!wifiFormVisible" :disabled="!connected || !capabilities.wifi_configuration" @click="editWifi">Edit Wi-Fi settings</button>
        <form v-if="wifiFormVisible" @submit.prevent="saveWifi" class="network-edit-form">
        <fieldset class="settings-fieldset" :disabled="!connected || !capabilities.wifi_configuration || wifiSaving">
        <div class="button-row"><button type="button" :disabled="wifiScanning" @click="scanWifi">{{ wifiScanning ? 'Scanning…' : 'Scan nearby networks' }}</button></div>
        <ul v-if="networks.length" class="network-list">
          <li v-for="network in networks" :key="network.ssid"><button type="button" @click="selectNetwork(network)">{{ network.ssid || '(hidden network)' }}</button><span> · {{ network.rssi }} dBm · {{ network.secure ? 'secured' : 'open' }}</span></li>
        </ul>
        <div class="configuration-form">
          <label class="configuration-field"><span>Wi-Fi name (SSID)</span><input v-model="wifiSsid" maxlength="32" autocomplete="off" required><small>Enter a visible or hidden network name.</small></label>
          <label class="configuration-field"><span>Password</span><input v-model="wifiPassword" maxlength="63" type="password" autocomplete="new-password"><small>Passwords are write-only and are not shown after saving.</small></label>
        </div>
        <div class="button-row configuration-actions"><button type="submit" class="primary">{{ wifiSaving ? 'Saving…' : 'Save and reconnect' }}</button><button v-if="!gateway?.setup_ap.active" type="button" @click="cancelWifi">Cancel</button></div>
        </fieldset></form>
        <p v-if="wifiResult" class="action-result" role="status">{{ wifiResult }}</p>
      </div>

      <div class="network-card">
        <div class="subsection-heading"><h3>Home Assistant MQTT</h3><span class="state-badge" :class="{ ready: connected && gateway?.mqtt_state === 'connected' }">{{ networkStateLabel(gateway?.mqtt_state) }}</span></div>
        <p class="network-name">{{ gateway?.mqtt.configured ? `${gateway.mqtt.host}:${gateway.mqtt.port}` : 'No broker configured' }}</p>
        <details class="technical-details"><summary>Connection details</summary><p v-if="gateway?.mqtt.username" class="muted">Username: {{ gateway.mqtt.username }}</p><p class="muted">Credentials are write-only. This local-LAN connection uses MQTT over TCP; do not expose the Gateway or broker to the Internet.</p></details>
        <p v-if="gateway?.setup_ap.active" class="muted">MQTT credentials can only be changed from the trusted station LAN, never through the open setup AP.</p>
        <template v-else>
          <button v-if="!mqttEditing" :disabled="!connected || !capabilities.mqtt_configuration" @click="editMqtt">Edit MQTT settings</button>
          <form v-if="mqttEditing" @submit.prevent="saveMqtt" class="network-edit-form"><fieldset class="settings-fieldset" :disabled="!connected || !capabilities.mqtt_configuration || mqttSaving">
          <div class="configuration-form">
            <label class="configuration-field"><span>Broker host or IP</span><input v-model="mqttHost" maxlength="63" autocomplete="off" required><small>Address of the local MQTT broker.</small></label>
            <label class="configuration-field"><span>Broker port</span><input v-model.number="mqttPort" type="number" min="1" max="65535" required><small>Use a port from 1 to 65535.</small></label>
            <label class="configuration-field"><span>Broker username</span><input v-model="mqttUsername" maxlength="63" autocomplete="off" required><small>MQTT credentials are write-only.</small></label>
            <label class="configuration-field"><span>Broker password</span><input v-model="mqttPassword" maxlength="63" type="password" autocomplete="new-password" required><small>Passwords are not displayed after saving.</small></label>
          </div>
          <div class="button-row configuration-actions"><button type="submit" class="primary">{{ mqttSaving ? 'Saving…' : 'Save MQTT settings' }}</button><button type="button" @click="cancelMqtt">Cancel</button></div>
          </fieldset></form>
          <p v-if="mqttResult" class="action-result" role="status">{{ mqttResult }}</p>
        </template>
      </div>
      </div>
    </section>
  </main>
</template>
