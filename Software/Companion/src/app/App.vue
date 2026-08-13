<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import ConnectionHeader from './ConnectionHeader.vue'
import DashboardView from '@/bms/DashboardView.vue'
import ServiceView from '@/bms/ServiceView.vue'
import WifiSettings from './WifiSettings.vue'
import { GatewayTransport } from '@/transports/GatewayTransport'
import { WebSerialTransport } from '@/transports/WebSerialTransport'
import type { BmsTransport, ConnectionState, GatewayStatus, Snapshot } from '@/transports/Transport'
const transport: BmsTransport = __FLEXBMS_TARGET__ === 'gateway' ? new GatewayTransport() : new WebSerialTransport()
const active = ref<'dashboard' | 'services' | 'wifi' | 'terminal'>('dashboard'); const state = ref<ConnectionState>('disconnected'); const snapshot = ref<Snapshot | null>(null); const gateway = ref<GatewayStatus>(); const raw = ref<string[]>([])
const capabilities = computed(() => transport.getCapabilities()); const fresh = computed(() => snapshot.value?.status.measurements_fresh ?? false)
const offSnapshot = transport.onSnapshot(next => { snapshot.value = next }); const offState = transport.onState((next, gatewayStatus) => { state.value = next; if (gatewayStatus) gateway.value = gatewayStatus }); const offRaw = transport.onRaw?.(line => { raw.value.push(line); if (raw.value.length > 100) raw.value.shift() })
async function connect(): Promise<void> { try { await transport.connect() } catch (error) { raw.value.push(error instanceof Error ? error.message : String(error)) } }
function disconnect(): void { transport.disconnect(); snapshot.value = null }
onBeforeUnmount(() => { offSnapshot(); offState(); offRaw?.(); transport.disconnect() })
</script>
<template><ConnectionHeader :label="transport.label" :state="state" :fresh="fresh" :gateway="gateway" @connect="connect" @disconnect="disconnect" /><nav><button @click="active = 'dashboard'">Dashboard</button><button @click="active = 'services'">Services</button><button v-if="capabilities.wifi_configuration" @click="active = 'wifi'">Wi-Fi</button><button v-if="capabilities.raw_terminal" @click="active = 'terminal'">Raw terminal</button></nav><DashboardView v-if="active === 'dashboard'" :snapshot="snapshot" /><ServiceView v-else-if="active === 'services'" :transport="transport" :capabilities="capabilities" :gateway="gateway" /><WifiSettings v-else-if="active === 'wifi'" :transport="transport" :capabilities="capabilities" :gateway="gateway" /><main v-else><p>USB service/development terminal. This is never sent through the Gateway.</p><pre>{{ raw.join('\n') }}</pre></main></template>
