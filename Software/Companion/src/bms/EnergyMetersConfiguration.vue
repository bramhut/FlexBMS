<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import type { BmsTransport, EnergyMeterConfiguration, EnergyMeterStatus } from '@/transports/Transport'

const props = defineProps<{ transport: BmsTransport; connected: boolean; allowed: boolean; status: EnergyMeterStatus }>()
const configuration = reactive<EnergyMeterConfiguration>({ baud_rate: 9600, parity: 'none', stop_bits: 1, meters: [] })
const dirty = ref(false)
const saving = ref(false)
const result = ref('')
const enabled = computed(() => configuration.meters.some(meter => meter.enabled))
function load() {
  Object.assign(configuration, { ...props.status.configuration, meters: props.status.configuration.meters.map(meter => ({ ...meter })) })
  dirty.value = false
}
watch(() => props.status.configuration, () => { if (!dirty.value && !saving.value) load() }, { immediate: true })
watch(() => props.connected, connected => { if (!connected) { dirty.value = false; result.value = '' } })
async function save() {
  if (!props.connected || !props.allowed || saving.value) return
  const active = configuration.meters.filter(meter => meter.enabled)
  if (configuration.meters.some(meter => !Number.isInteger(meter.address) || meter.address < 1 || meter.address > 247 ||
      new TextEncoder().encode(meter.name).length > 48 || /[\u0000-\u001f\u007f]/.test(meter.name) || (meter.enabled && !meter.name.trim())) ||
      new Set(active.map(meter => meter.address)).size !== active.length) {
    result.value = 'Enter a name of at most 48 bytes and an address from 1 to 247 for each meter. Enabled meters need different addresses.'
    return
  }
  saving.value = true
  const saved = { ...configuration, meters: configuration.meters.map(meter => ({ ...meter })) }
  try {
    const response = await props.transport.configureEnergyMeters(saved)
    result.value = response.result === 'accepted' ? 'Saved. The Gateway is applying the meter settings.' : `Could not save meter settings: ${response.result}.`
    if (response.result === 'accepted') dirty.value = false
  } catch { result.value = 'Could not save meter settings: connection error.' }
  finally { saving.value = false }
}
function age(ms: number | null) { return ms === null ? 'No successful read yet' : `${(ms / 1000).toFixed(1)} s ago` }
</script>

<template>
  <section class="panel energy-meters-panel">
    <div class="panel-heading"><div><h2>RS485 energy meters</h2></div><p>Read up to two SDM72D-M-2 meters and report them to Home Assistant through MQTT.</p></div>
    <p class="muted">Bus: {{ connected ? status.bus_state : 'disconnected' }}. Electrical readings update every second; energy counters every 30 seconds.</p>
    <p v-if="!status.stored_configuration_valid" class="action-result">Saved meter settings could not be read. Both meters are disabled until you save valid settings.</p>
    <p v-if="!allowed" class="muted">Meter settings can be changed through the Gateway on the trusted station LAN.</p>
    <form @submit.prevent="save" @input="dirty = true" @change="dirty = true">
      <fieldset :disabled="!connected || !allowed || saving" class="meter-settings">
        <div v-for="(meter, slot) in configuration.meters" :key="slot" class="configuration-subsection">
          <div class="subsection-heading"><h3>Meter {{ slot + 1 }}</h3></div>
          <label class="configuration-checkbox"><input v-model="meter.enabled" type="checkbox"><span>Enable meter {{ slot + 1 }}</span></label>
          <div class="configuration-form">
            <label class="configuration-field"><span>Name in Home Assistant</span><input v-model="meter.name" maxlength="48" :disabled="!meter.enabled" :required="meter.enabled"><small>For example, Schuur feeder or Heat pump.</small></label>
            <label class="configuration-field"><span>Modbus address</span><input v-model.number="meter.address" type="number" min="1" max="247" step="1" :disabled="!meter.enabled"><small>Set a different address on each physical meter.</small></label>
          </div>
          <label class="configuration-checkbox"><input v-model="meter.reverse_power_direction" type="checkbox" :disabled="!meter.enabled"><span>Reverse power direction</span></label>
          <small>Reverses signed power and exchanges import and export energy. Currents remain positive.</small>
        </div>
        <div v-if="enabled" class="configuration-subsection">
          <div class="subsection-heading"><h3>Shared RS485 connection</h3></div>
          <div class="configuration-form">
            <label class="configuration-field"><span>Baud rate</span><select v-model.number="configuration.baud_rate"><option v-for="baud in [1200, 2400, 4800, 9600, 19200]" :key="baud" :value="baud">{{ baud }}</option></select><small>Match the setting on both physical meters.</small></label>
          </div>
          <details><summary>Advanced serial settings</summary><div class="configuration-form">
            <label class="configuration-field"><span>Parity</span><select v-model="configuration.parity"><option value="none">None</option><option value="even">Even</option><option value="odd">Odd</option></select></label>
            <label class="configuration-field"><span>Stop bits</span><select v-model.number="configuration.stop_bits"><option :value="1">1</option><option :value="2">2</option></select></label>
          </div></details>
        </div>
        <div class="button-row configuration-actions"><button type="submit" class="primary" :disabled="!dirty">{{ saving ? 'Saving…' : 'Save meter settings' }}</button><button type="button" @click="load">Discard changes</button></div>
      </fieldset>
    </form>
    <p v-if="result" class="action-result" role="status">{{ result }}</p>
    <div v-for="(meter, slot) in status.meters" :key="slot">
      <div v-if="status.configuration.meters[slot]?.enabled" class="configuration-subsection">
        <div class="subsection-heading"><h3>{{ status.configuration.meters[slot]?.name }}</h3><p>{{ connected && meter.available ? 'Available' : 'Unavailable' }} · Last read: {{ age(meter.last_success_age_ms) }} · Failed reads: {{ meter.failed_reads }}</p></div>
        <p v-if="connected && meter.available && meter.electrical">L1 {{ meter.electrical.current_l1_a.toFixed(2) }} A · L2 {{ meter.electrical.current_l2_a.toFixed(2) }} A · L3 {{ meter.electrical.current_l3_a.toFixed(2) }} A · Total {{ meter.electrical.total_power_w.toFixed(0) }} W</p>
        <p v-if="connected && meter.energy_fresh && meter.energy">Import {{ meter.energy.import_kwh.toFixed(3) }} kWh · Export {{ meter.energy.export_kwh.toFixed(3) }} kWh</p>
        <p v-if="meter.serial_number !== undefined" class="muted">Serial {{ meter.serial_number }} · Model code {{ meter.meter_code }}<span v-if="meter.firmware_version"> · Firmware {{ meter.firmware_version }}</span></p>
        <p v-if="meter.last_error" class="muted">Last read error: {{ meter.last_error }}</p>
      </div>
    </div>
  </section>
</template>

<style scoped>
.meter-settings { border: 0; padding: 0; margin: 0; min-width: 0; }
details { margin-top: 1rem; }
summary { cursor: pointer; margin-bottom: 1rem; }
</style>
