<script setup lang="ts">
import { computed, ref } from 'vue'
import type { Capabilities, GatewayStatus } from '@/transports/Transport'

type Target = 'gateway' | 'stm32'
type Manifest = { target: Target; version: string; image_file: string; image_bytes: number; image_crc32: string }

const props = defineProps<{ capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()
const manifestFiles = ref<Partial<Record<Target, File>>>({})
const imageFiles = ref<Partial<Record<Target, File>>>({})
const manifests = ref<Partial<Record<Target, Manifest>>>({})
const result = ref('')
const uploading = ref<Target | null>(null)
const canUpdate = computed(() => props.connected && props.capabilities.firmware_update && props.gateway?.wifi_state === 'connected' && !props.gateway?.setup_ap.active)
const update = computed(() => props.gateway?.firmware_update)

function selectedFile(event: Event): File | undefined { return (event.target as HTMLInputElement).files?.[0] }
async function chooseManifest(target: Target, event: Event): Promise<void> {
  const file = selectedFile(event); manifestFiles.value[target] = file; manifests.value[target] = undefined; result.value = ''
  if (!file) return
  try {
    const parsed = JSON.parse(await file.text()) as Manifest
    if ((parsed.target !== 'gateway' && parsed.target !== 'stm32') || parsed.target !== target || typeof parsed.version !== 'string' || !Number.isInteger(parsed.image_bytes) || parsed.image_bytes <= 0 || !/^[0-9a-fA-F]{8}$/.test(parsed.image_crc32) || typeof parsed.image_file !== 'string') throw new Error('invalid')
    manifests.value[target] = parsed
  } catch { result.value = 'Choose a valid FlexBMS firmware manifest for this target.' }
}
function chooseImage(target: Target, event: Event): void { imageFiles.value[target] = selectedFile(event); result.value = '' }
async function upload(target: Target): Promise<void> {
  const manifest = manifests.value[target]; const image = imageFiles.value[target]
  if (!canUpdate.value || !manifest || !image) return
  if (image.name !== manifest.image_file || image.size !== manifest.image_bytes) { result.value = 'The selected binary does not match its manifest.'; return }
  uploading.value = target; result.value = ''
  try {
    const response = await fetch(`/api/firmware/${target}`, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'X-FlexBMS-Target': manifest.target, 'X-FlexBMS-Version': manifest.version, 'X-FlexBMS-Length': String(manifest.image_bytes), 'X-FlexBMS-CRC32': manifest.image_crc32 }, body: image })
    result.value = response.ok ? `${target === 'gateway' ? 'Gateway' : 'STM32'} image accepted. ${target === 'gateway' ? 'The Gateway will reboot.' : 'Programming starts automatically.'}` : `Upload failed: ${await response.text()}`
  } catch { result.value = 'Upload failed because the Gateway connection was lost.' }
  finally { uploading.value = null }
}
</script>

<template>
  <section v-if="capabilities.firmware_update || update?.phase !== 'idle'" class="panel firmware-update-panel">
    <div class="panel-heading"><div><p class="eyebrow">Personal service</p><h2>Firmware update</h2></div><p>Station LAN only. Wired release flashing remains the recovery path.</p></div>
    <p v-if="!canUpdate" class="warning-text">Firmware updates are available only while the Gateway is connected to its station network.</p>
    <div v-for="target in (['gateway', 'stm32'] as Target[])" :key="target" class="firmware-target">
      <h3>{{ target === 'gateway' ? 'ESP32 Gateway' : 'STM32 BMS' }}</h3>
      <label>Release manifest<input type="file" accept="application/json,.json" :disabled="!canUpdate || uploading !== null" @change="chooseManifest(target, $event)"></label>
      <label>Firmware binary<input type="file" accept=".bin,application/octet-stream" :disabled="!canUpdate || uploading !== null" @change="chooseImage(target, $event)"></label>
      <p v-if="manifests[target]" class="muted">{{ manifests[target]?.version }} · {{ manifests[target]?.image_bytes.toLocaleString() }} bytes</p>
      <button class="danger" :disabled="!canUpdate || !manifests[target] || !imageFiles[target] || uploading !== null" @click="upload(target)">{{ uploading === target ? 'Uploading…' : `Upload and install ${target === 'gateway' ? 'Gateway' : 'STM32'}` }}</button>
    </div>
    <p v-if="update && update.phase !== 'idle'" class="action-result">{{ update.target }}: {{ update.phase }} — {{ update.detail }}</p>
    <p v-if="result" class="action-result">{{ result }}</p>
  </section>
</template>
