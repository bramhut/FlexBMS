<script setup lang="ts">
import { computed } from 'vue'
import { bmsFaultNames, bmsStateName, hvReasonNames, hvStateName, setBits, warningDisplayNames } from '@/shared/model'
import type { RecordedControllerEvent } from '@/transports/Transport'

const props = defineProps<{ events: RecordedControllerEvent[]; limit?: number }>()

const visibleEvents = computed(() => props.events.slice(0, props.limit))
const friendlyName = (name: string) => name.toLowerCase().replace(/_/g, ' ').replace(/\b[a-z]/g, (letter: string) => letter.toUpperCase())
const eventMask = (value: number, names: string[], empty: string) => {
  const namesSet = setBits(value, names).map(friendlyName)
  return namesSet.length > 0 ? namesSet.join(', ') : empty
}
const warningMask = (value: number) => setBits(value, warningDisplayNames).join(', ') || 'Cleared'
const eventDescription = (event: RecordedControllerEvent): { title: string; detail: string } => {
  switch (event.event_id) {
    case 1: return { title: 'BMS state', detail: bmsStateName(event.value) }
    case 2: return { title: 'HV state', detail: hvStateName(event.value) }
    case 3: return { title: 'BMS active errors', detail: eventMask(event.value, bmsFaultNames, 'Cleared') }
    case 4: return { title: 'BMS latched errors', detail: eventMask(event.value, bmsFaultNames, 'Cleared') }
    case 5: return { title: 'HV active errors', detail: eventMask(event.value, hvReasonNames, 'Cleared') }
    case 6: return { title: 'HV latched errors', detail: eventMask(event.value, hvReasonNames, 'Cleared') }
    case 7: return { title: 'Warnings', detail: warningMask(event.value) }
    case 8: return { title: 'Measurements', detail: event.value === 0 ? 'No longer fresh' : 'Fresh' }
    default: return { title: `Event ${event.event_id}`, detail: `Value ${event.value}` }
  }
}
const eventTime = (event: RecordedControllerEvent) => new Intl.DateTimeFormat(undefined, { timeStyle: 'medium' }).format(new Date(event.observed_at_ms))
</script>

<template>
  <ol class="event-list">
    <li v-for="event in visibleEvents" :key="`${event.observed_at_ms}-${event.event_id}`">
      <time>{{ eventTime(event) }}</time>
      <div><strong>{{ eventDescription(event).title }}</strong><small>{{ eventDescription(event).detail }}</small></div>
    </li>
  </ol>
</template>
