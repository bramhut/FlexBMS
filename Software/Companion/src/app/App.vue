<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import ConnectionHeader from './ConnectionHeader.vue'
import DashboardView from '@/bms/DashboardView.vue'
import WifiSettings from './WifiSettings.vue'
import { GatewayTransport } from '@/transports/GatewayTransport'
import { WebSerialTransport } from '@/transports/WebSerialTransport'
import type { BmsTransport, ConnectionState, GatewayStatus, Snapshot, Status } from '@/transports/Transport'
const transport: BmsTransport = __FLEXBMS_TARGET__ === 'gateway' ? new GatewayTransport() : new WebSerialTransport()
const active = ref<'dashboard' | 'wifi'>('dashboard'); const state = ref<ConnectionState>('disconnected'); const snapshot = ref<Snapshot | null>(null); const bmsStatus = ref<Status | null>(null); const gateway = ref<GatewayStatus>()
const capabilities = ref(transport.getCapabilities()); const fresh = computed(() => bmsStatus.value?.measurements_fresh ?? false)
const offBmsStatus = transport.onBmsStatus(next => { bmsStatus.value = next }); const offSnapshot = transport.onSnapshot(next => { snapshot.value = next; bmsStatus.value = next.status }); const offState = transport.onState((next, gatewayStatus) => { state.value = next; capabilities.value = transport.getCapabilities(); if (gatewayStatus) gateway.value = gatewayStatus })
async function connect(): Promise<void> { try { await transport.connect(); capabilities.value = transport.getCapabilities() } catch (error) { console.error('Companion connection failed', error) } }
function disconnect(): void { transport.disconnect(); snapshot.value = null; bmsStatus.value = null }
onBeforeUnmount(() => { offBmsStatus(); offSnapshot(); offState(); transport.disconnect() })
</script>
<template><ConnectionHeader :label="transport.label" :state="state" :fresh="fresh" :gateway="gateway" @connect="connect" @disconnect="disconnect" /><nav><button :class="{ active: active === 'dashboard' }" @click="active = 'dashboard'">BMS dashboard</button><button v-if="capabilities.wifi_configuration" :class="{ active: active === 'wifi' }" @click="active = 'wifi'">Wi-Fi setup</button></nav><DashboardView v-if="active === 'dashboard'" :snapshot="snapshot" :status="bmsStatus" :transport="transport" :capabilities="capabilities" :connected="state === 'connected'" :gateway="gateway" /><WifiSettings v-else :transport="transport" :capabilities="capabilities" :gateway="gateway" /></template>
