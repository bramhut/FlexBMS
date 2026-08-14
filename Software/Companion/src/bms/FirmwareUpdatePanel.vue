<script setup lang="ts">
import { computed, ref } from 'vue'
import { parseFirmwareBundle, type FirmwareBundle, type FirmwareTarget } from '@/shared/firmwareBundle'
import type { Capabilities, FirmwareUpdateStage, GatewayStatus } from '@/transports/Transport'

const props = defineProps<{ capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()
const bundleFile = ref<File>()
const bundle = ref<FirmwareBundle>()
const selected = ref<Record<FirmwareTarget, boolean>>({ stm32: true, gateway: true })
const result = ref('')
const uploading = ref(false)
const currentTarget = ref<FirmwareTarget | null>(null)
const canUpdate = computed(() => props.connected && props.capabilities.firmware_update && props.gateway?.wifi_state === 'connected' && !props.gateway?.setup_ap.active)
const update = computed(() => props.gateway?.firmware_update)
const selectedTargets = computed(() => (['stm32', 'gateway'] as FirmwareTarget[]).filter(target => selected.value[target]))

type FlowStep = { stage: FirmwareUpdateStage; label: string }
const flowSteps = computed<FlowStep[]>(() => update.value?.target === 'stm32'
  ? [{ stage: 'upload', label: 'Upload' }, { stage: 'handoff', label: 'Safe off' }, { stage: 'rom', label: 'ROM' }, { stage: 'erase', label: 'Erase' }, { stage: 'program', label: 'Program' }, { stage: 'verify', label: 'Verify' }, { stage: 'restart', label: 'Restart' }, { stage: 'complete', label: 'Online' }]
  : [{ stage: 'upload', label: 'Upload' }, { stage: 'restart', label: 'Restart' }, { stage: 'complete', label: 'Online' }])
const currentStage = computed<FirmwareUpdateStage>(() => update.value?.stage ?? (update.value?.phase === 'uploading' ? 'upload' : 'idle'))
const activeStepIndex = computed(() => flowSteps.value.findIndex(step => step.stage === currentStage.value))
const byteProgressStages: FirmwareUpdateStage[] = ['upload', 'program', 'verify']
const progressPercent = computed(() => {
  const current = update.value
  if (!current || current.expected_bytes === 0) return 0
  if (current.phase === 'complete') return 100
  return Math.min(100, Math.round(((current.progress_bytes ?? current.received_bytes) / current.expected_bytes) * 100))
})
const progressText = computed(() => {
  const current = update.value
  if (!current) return ''
  if (!byteProgressStages.includes(currentStage.value)) return current.detail
  return `${current.detail} - ${progressPercent.value}% (${(current.progress_bytes ?? current.received_bytes).toLocaleString()} / ${current.expected_bytes.toLocaleString()} bytes)`
})

function flowState(index: number): 'complete' | 'active' | 'pending' | 'failed' {
  const current = update.value
  if (!current) return 'pending'
  if (current.phase === 'complete') return 'complete'
  if (index < activeStepIndex.value) return 'complete'
  if (index === activeStepIndex.value) return current.phase === 'failed' ? 'failed' : 'active'
  return 'pending'
}
function selectedFile(event: Event): File | undefined { return (event.target as HTMLInputElement).files?.[0] }
function targetName(target: FirmwareTarget): string { return target === 'gateway' ? 'ESP32 Gateway' : 'STM32 BMS' }
async function chooseBundle(event: Event): Promise<void> {
  const input = event.target as HTMLInputElement
  bundleFile.value = selectedFile(event); bundle.value = undefined; result.value = ''
  if (!bundleFile.value) return
  if (!bundleFile.value.name.toLowerCase().endsWith('.fbu')) { input.value = ''; bundleFile.value = undefined; result.value = 'Choose a FlexBMS_bundle.fbu file.'; return }
  try {
    bundle.value = await parseFirmwareBundle(bundleFile.value)
    selected.value = { stm32: true, gateway: true }
  } catch { result.value = 'Choose a valid FlexBMS_bundle.fbu file.' }
}
async function upload(target: FirmwareTarget): Promise<boolean> {
  const file = bundleFile.value; const image = bundle.value?.images[target]
  if (!file || !image) return false
  currentTarget.value = target
  const response = await fetch(`/api/firmware/${target}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/octet-stream', 'X-FlexBMS-Target': image.target, 'X-FlexBMS-Version': image.version, 'X-FlexBMS-Length': String(image.image_bytes), 'X-FlexBMS-CRC32': image.image_crc32 },
    body: file.slice(image.offset, image.offset + image.image_bytes),
  })
  if (!response.ok) { result.value = `${targetName(target)} upload failed: ${await response.text()}`; return false }
  return true
}
async function waitForStm32(): Promise<boolean> {
  const deadline = Date.now() + 120000
  let sawCurrentInstall = false
  while (Date.now() < deadline) {
    const status = props.gateway?.firmware_update
    if (status?.target === 'stm32' && (status.phase === 'uploading' || status.phase === 'installing')) sawCurrentInstall = true
    if (status?.target === 'stm32' && sawCurrentInstall && status.phase === 'complete') return true
    if (status?.target === 'stm32' && status.phase === 'failed') { result.value = `STM32 update failed: ${status.detail}`; return false }
    await new Promise(resolve => window.setTimeout(resolve, 250))
  }
  result.value = 'STM32 update did not report completion.'
  return false
}
async function installSelected(): Promise<void> {
  if (!canUpdate.value || !bundle.value || selectedTargets.value.length === 0 || uploading.value) return
  uploading.value = true; result.value = ''
  try {
    if (selected.value.stm32) {
      if (!await upload('stm32')) return
      result.value = 'Programming STM32 BMS...'
      if (!await waitForStm32()) return
    }
    if (selected.value.gateway) {
      if (!await upload('gateway')) return
      result.value = 'Gateway image accepted. The Gateway will reboot.'
    }
    if (!selected.value.gateway) result.value = 'Selected firmware installed.'
  } catch { result.value = 'Upload failed because the Gateway connection was lost.' }
  finally { uploading.value = false; currentTarget.value = null }
}
</script>

<template>
  <section v-if="capabilities.firmware_update || update?.phase !== 'idle'" class="panel firmware-update-panel">
    <div class="panel-heading"><div><h2>Firmware update</h2></div><p>Station LAN only. Wired release flashing remains the recovery path.</p></div>
    <p v-if="!canUpdate" class="warning-text">Firmware updates are available only while the Gateway is connected to its station network.</p>
    <label class="bundle-picker">FlexBMS update bundle<input type="file" accept=".fbu" :disabled="!canUpdate || uploading" @change="chooseBundle($event)"></label>
    <template v-if="bundle">
      <p class="muted">Bundle version {{ bundle.version }}. Select the controllers to update.</p>
      <div v-for="target in (['stm32', 'gateway'] as FirmwareTarget[])" :key="target" class="firmware-target">
        <label class="firmware-choice"><input v-model="selected[target]" type="checkbox" :disabled="uploading"> <span><b>{{ targetName(target) }}</b><small>{{ bundle.images[target].version }} - {{ bundle.images[target].image_bytes.toLocaleString() }} bytes</small></span></label>
      </div>
      <button class="danger" :disabled="!canUpdate || selectedTargets.length === 0 || uploading" @click="installSelected">{{ uploading ? `${currentTarget ? `Uploading ${targetName(currentTarget)}...` : 'Installing...'}` : `Install ${selectedTargets.length === 2 ? 'selected firmware' : targetName(selectedTargets[0])}` }}</button>
    </template>
    <div v-if="update && update.phase !== 'idle'" class="firmware-progress-panel" aria-live="polite">
      <div class="firmware-flow" :aria-label="`${targetName(update.target)} update flow`">
        <div v-for="(step, index) in flowSteps" :key="step.stage" class="firmware-flow-step" :class="flowState(index)">
          <span class="firmware-flow-marker">{{ flowState(index) === 'complete' ? 'Done' : index + 1 }}</span>
          <span>{{ step.label }}</span>
        </div>
      </div>
      <div class="firmware-progress-track" role="progressbar" :aria-valuenow="progressPercent" aria-valuemin="0" aria-valuemax="100">
        <div class="firmware-progress-value" :style="{ width: `${progressPercent}%` }"></div>
      </div>
      <p class="action-result">{{ progressText }}</p>
    </div>
    <p v-if="result" class="action-result">{{ result }}</p>
  </section>
</template>
