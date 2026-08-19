<script setup lang="ts">
import { computed, ref } from 'vue'
import type { BmsTransport, Capabilities, GatewayStatus, WifiNetwork } from '@/transports/Transport'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; gateway?: GatewayStatus }>()
const ssid = ref('')
const password = ref('')
const networks = ref<WifiNetwork[]>([])
const result = ref('')
const scanning = ref(false)
const saving = ref(false)
const mqttHost = ref(props.gateway?.mqtt.host ?? '')
const mqttPort = ref(props.gateway?.mqtt.port ?? 1883)
const mqttUsername = ref(props.gateway?.mqtt.username ?? '')
const mqttPassword = ref('')
const mqttSaving = ref(false)
const mqttResult = ref('')

const apMessage = computed(() => {
  const ap = props.gateway?.setup_ap
  return ap?.active ? `Setup AP ${ap.ssid} is active. Open http://${ap.address}.` : ''
})

function selectNetwork(network: WifiNetwork): void { ssid.value = network.ssid }

async function scan(): Promise<void> {
  if (!props.capabilities.wifi_configuration || scanning.value) return
  scanning.value = true
  result.value = ''
  const response = await props.transport.scanWifi()
  scanning.value = false
  if (response.result === 'ok')
  {
    networks.value = response.networks ?? []
    result.value = networks.value.length === 0 ? 'No nearby networks found.' : 'Select a network or enter a hidden SSID manually.'
    return
  }
  result.value = response.result === 'rate_limited' ? 'Please wait before scanning again.' : `Scan unavailable: ${response.result}.`
}

async function save(): Promise<void> {
  if (!props.capabilities.wifi_configuration || saving.value) return
  if (ssid.value.length === 0 || ssid.value.length > 32 || password.value.length > 63)
  {
    result.value = 'SSID must be 1–32 bytes and password at most 63 bytes.'
    return
  }
  saving.value = true
  const response = await props.transport.configureWifi(ssid.value, password.value)
  saving.value = false
  result.value = response.result === 'accepted'
    ? 'Saved. The Gateway is restarting Wi-Fi; reconnect on the new network or its recovery AP if needed.'
    : `Could not save Wi-Fi settings: ${response.result}.`
}

async function saveMqtt(): Promise<void> {
  if (!props.capabilities.mqtt_configuration || mqttSaving.value) return
  if (mqttHost.value.length === 0 || mqttHost.value.length > 63 || mqttUsername.value.length === 0 || mqttUsername.value.length > 63 || mqttPassword.value.length === 0 || mqttPassword.value.length > 63 || mqttPort.value < 1 || mqttPort.value > 65535)
  {
    mqttResult.value = 'Enter a broker host, port 1–65535, username, and password (each at most 63 bytes).'
    return
  }
  mqttSaving.value = true
  const response = await props.transport.configureMqtt(mqttHost.value, mqttPort.value, mqttUsername.value, mqttPassword.value)
  mqttSaving.value = false
  mqttResult.value = response.result === 'accepted' ? 'Saved. The Gateway is connecting to MQTT on the station LAN.' : `Could not save MQTT settings: ${response.result}.`
  if (response.result === 'accepted') mqttPassword.value = ''
}
</script>

<template>
  <main class="stack">
    <section>
      <h2>Gateway Wi-Fi</h2>
      <p>State: {{ gateway?.wifi_state ?? 'unavailable' }}<span v-if="gateway?.wifi_ssid"> · {{ gateway.wifi_ssid }}</span></p>
      <p v-if="apMessage">{{ apMessage }}</p>
      <p v-if="gateway?.setup_ap.active">The setup AP is open temporarily. Monitoring and Wi-Fi setup are available there; BMS service controls are disabled.</p>
    </section>
    <section>
      <h2>Choose network</h2>
      <button :disabled="!capabilities.wifi_configuration || scanning" @click="scan">{{ scanning ? 'Scanning…' : 'Scan nearby networks' }}</button>
      <ul v-if="networks.length">
        <li v-for="network in networks" :key="network.ssid">
          <button @click="selectNetwork(network)">{{ network.ssid || '(hidden network)' }}</button>
          <span> · {{ network.rssi }} dBm · {{ network.secure ? 'secured' : 'open' }}</span>
        </li>
      </ul>
      <label>Wi-Fi name (SSID)<input v-model="ssid" maxlength="32" autocomplete="off" required></label>
      <label>Password<input v-model="password" maxlength="63" type="password" autocomplete="new-password"></label>
      <button :disabled="!capabilities.wifi_configuration || saving" @click="save">{{ saving ? 'Saving…' : 'Save and reconnect' }}</button>
      <small>Passwords are write-only and are not shown after saving. Hidden networks can be entered manually.</small>
      <p v-if="result"><b>{{ result }}</b></p>
    </section>
    <section>
      <h2>Home Assistant MQTT</h2>
      <p>State: {{ gateway?.mqtt_state ?? 'unavailable' }}<span v-if="gateway?.mqtt.configured"> · {{ gateway.mqtt.host }}:{{ gateway.mqtt.port }} · {{ gateway.mqtt.username }}</span></p>
      <p v-if="gateway?.setup_ap.active">MQTT credentials can only be changed from the trusted station LAN, never through the open setup AP.</p>
      <template v-else>
        <label>Broker host or IP<input v-model="mqttHost" maxlength="63" autocomplete="off" required></label>
        <label>Broker port<input v-model.number="mqttPort" type="number" min="1" max="65535" required></label>
        <label>Broker username<input v-model="mqttUsername" maxlength="63" autocomplete="off" required></label>
        <label>Broker password<input v-model="mqttPassword" maxlength="63" type="password" autocomplete="new-password" required></label>
        <button :disabled="!capabilities.mqtt_configuration || mqttSaving" @click="saveMqtt">{{ mqttSaving ? 'Saving…' : 'Save MQTT settings' }}</button>
        <small>Credentials are write-only. This v1 local-LAN connection uses normal MQTT over TCP; do not expose the Gateway or broker to the Internet.</small>
        <p v-if="mqttResult"><b>{{ mqttResult }}</b></p>
      </template>
    </section>
  </main>
</template>
