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
  </main>
</template>
