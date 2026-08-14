<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import ConnectionHeader from './ConnectionHeader.vue'
import DashboardView from '@/bms/DashboardView.vue'
import FirmwareView from '@/bms/FirmwareView.vue'
import WifiSettings from './WifiSettings.vue'
import { GatewayTransport } from '@/transports/GatewayTransport'
import { WebSerialTransport } from '@/transports/WebSerialTransport'
import type { BmsTransport, ConnectionState, GatewayStatus, Snapshot, Status } from '@/transports/Transport'
const gatewayTarget = __FLEXBMS_TARGET__ === 'gateway'
const transport: BmsTransport = gatewayTarget ? new GatewayTransport() : new WebSerialTransport()
const active = ref<'dashboard' | 'wifi' | 'firmware'>('dashboard'); const state = ref<ConnectionState>('disconnected'); const snapshot = ref<Snapshot | null>(null); const bmsStatus = ref<Status | null>(null); const gateway = ref<GatewayStatus>()
const capabilities = ref(transport.getCapabilities()); const fresh = computed(() => Boolean(bmsStatus.value?.measurements_fresh) && (!gateway.value || gateway.value.uart_state === 'healthy'))
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
const offBmsStatus = transport.onBmsStatus(next => { bmsStatus.value = next }); const offSnapshot = transport.onSnapshot(next => { snapshot.value = next; bmsStatus.value = next.status }); const offState = transport.onState((next, gatewayStatus) => { state.value = next; capabilities.value = transport.getCapabilities(); if (gatewayStatus) gateway.value = gatewayStatus })
async function connect(): Promise<void> { try { await transport.connect(); capabilities.value = transport.getCapabilities() } catch (error) { console.error('Companion connection failed', error) } }
onMounted(() => { if (gatewayTarget) void connect() })
onBeforeUnmount(() => { offBmsStatus(); offSnapshot(); offState(); transport.disconnect() })
</script>
<template><ConnectionHeader :label="transport.label" :message="connectionMessage" :tone="connectionTone" :allow-manual-connect="!gatewayTarget && state === 'disconnected'" @connect="connect" /><nav><button :class="{ active: active === 'dashboard' }" @click="active = 'dashboard'">BMS dashboard</button><button v-if="capabilities.wifi_configuration" :class="{ active: active === 'wifi' }" @click="active = 'wifi'">Wi-Fi setup</button><button :class="{ active: active === 'firmware' }" @click="active = 'firmware'">Firmware</button></nav><DashboardView v-if="active === 'dashboard'" :snapshot="snapshot" :status="bmsStatus" :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" /><WifiSettings v-else-if="active === 'wifi'" :transport="transport" :capabilities="capabilities" :gateway="gateway" /><FirmwareView v-else :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" /></template>
