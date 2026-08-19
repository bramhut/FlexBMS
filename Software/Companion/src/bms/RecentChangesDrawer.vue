<script setup lang="ts">
import RecentChangesList from './RecentChangesList.vue'
import type { RecordedControllerEvent } from '@/transports/Transport'

defineProps<{ events: RecordedControllerEvent[]; open: boolean }>()
const emit = defineEmits<{ close: [] }>()
</script>

<template>
  <div v-if="open" class="event-drawer-layer">
    <button class="event-drawer-scrim" aria-label="Close recent changes" @click="emit('close')"></button>
    <aside class="event-drawer" role="dialog" aria-modal="true" aria-labelledby="recent-changes-title">
      <div class="panel-heading">
        <div><h2 id="recent-changes-title">Recent changes</h2></div>
        <button @click="emit('close')">Close</button>
      </div>
      <p class="muted">This Companion session · newest first</p>
      <p v-if="events.length === 0" class="muted">No controller changes recorded yet.</p>
      <RecentChangesList v-else :events="events" />
    </aside>
  </div>
</template>
