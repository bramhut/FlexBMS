<script setup lang="ts">
import { ref } from 'vue'
import type { BmsTransport, Capabilities, ServiceResponse } from '@/transports/Transport'
import { serviceResultLabel } from '@/shared/service'

const props = defineProps<{ transport: BmsTransport; capabilities: Capabilities; connected: boolean; acknowledgementPending: boolean }>()
const result = ref('')

const availability = (capability: keyof Capabilities) => props.capabilities[capability] ? '' : 'Unavailable in the current Gateway state.'

function describe(response: ServiceResponse): string {
  if (response.data?.value !== undefined) return `${serviceResultLabel(response.result)}: 0x${response.data.value.toString(16).padStart(4, '0').toUpperCase()}`
  return serviceResultLabel(response.result)
}

async function acknowledgeFaults(): Promise<void> {
  const response = await props.transport.request('acknowledge_faults', {})
  result.value = `Fault acknowledgement: ${describe(response)}`
}
</script>

<template>
  <section v-if="acknowledgementPending" class="panel controls-panel recovery-panel">
    <div class="panel-heading"><div><h2>Error recovery</h2></div><p>Named requests only; the STM32 remains the safety authority.</p></div>
    <div class="control-layout">
      <div class="control-section recovery-section">
        <p>Acknowledgement clears resolved error latches and a recorded watchdog reset. Live and configuration warnings remain until their condition changes.</p>
        <button class="danger" :disabled="!capabilities.acknowledge_faults" @click="acknowledgeFaults">Acknowledge resolved faults</button>
        <small v-if="!capabilities.acknowledge_faults">{{ availability('acknowledge_faults') }}</small>
      </div>
    </div>
    <p v-if="result" class="action-result">{{ result }}</p>
  </section>
</template>
