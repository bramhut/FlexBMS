<script setup lang="ts">
defineProps<{ label: string; message: string; tone: 'healthy' | 'waiting' | 'problem'; allowManualConnect: boolean }>()
const emit = defineEmits<{ connect: [] }>()
</script>

<template>
  <header class="connection-header" :data-tone="tone">
    <div class="connection-status"><span class="status-dot" aria-hidden="true"></span><strong>{{ label }}</strong><span>{{ message }}</span></div>
    <button v-if="allowManualConnect" @click="emit('connect')">Connect USB</button>
  </header>
</template>
