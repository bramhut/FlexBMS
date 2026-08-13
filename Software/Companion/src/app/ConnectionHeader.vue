<script setup lang="ts">
import type { ConnectionState, GatewayStatus } from '@/transports/Transport'
defineProps<{ label: string; state: ConnectionState; fresh: boolean; gateway?: GatewayStatus }>()
const emit = defineEmits<{ connect: []; disconnect: [] }>()
</script>
<template>
  <header class="connection-header"><div><strong>{{ label }}</strong><span class="muted"> · {{ state }} · {{ fresh ? 'telemetry fresh' : 'telemetry stale' }}</span><span v-if="gateway" class="muted"> · Wi-Fi {{ gateway.wifi_state }} · UART {{ gateway.uart_state }}</span></div><button v-if="state === 'disconnected'" @click="emit('connect')">Connect</button><button v-else @click="emit('disconnect')">Disconnect</button></header>
</template>
