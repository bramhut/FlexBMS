<script setup lang="ts">
import { computed, reactive, ref, watch } from 'vue'
import type { BmsTransport, EnergyMeterConfiguration, EnergyMeterStatus } from '@/transports/Transport'

const props = defineProps<{ transport: BmsTransport; connected: boolean; allowed: boolean; status: EnergyMeterStatus }>()
const configuration = reactive<EnergyMeterConfiguration>({ baud_rate: 9600, parity: 'none', stop_bits: 1, meters: [] })
const dirty = ref(false)
const saving = ref(false)
const result = ref('')
const enabled = computed(() => configuration.meters.some(meter => meter.enabled))
const busLabel = computed(() => !props.connected ? 'Disconnected' : ({
  disabled: 'Disabled', starting: 'Starting', ready: 'Ready', error: 'Error', unavailable: 'Unavailable',
})[props.status.bus_state])
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
    <div class="panel-heading"><div><h2>RS485 energy meters</h2><p>Up to two SDM72D-M-2 meters, with Home Assistant discovery through MQTT.</p></div></div>
    <div class="meter-overview"><span class="meter-state" :class="{ available: connected && status.bus_state === 'ready' }">Bus: {{ busLabel }}</span><span class="muted">Power and current: every 1 s · Energy: every 30 s</span></div>
    <p v-if="!status.stored_configuration_valid" class="action-result">Saved meter settings could not be read. Both meters are disabled until you save valid settings.</p>
    <p v-if="!allowed" class="muted">Meter settings can be changed through the Gateway on the trusted station LAN.</p>
    <form @submit.prevent="save" @input="dirty = true" @change="dirty = true">
      <fieldset :disabled="!connected || !allowed || saving" class="meter-settings">
        <div class="meter-slots">
          <div v-for="(meter, slot) in configuration.meters" :key="slot" class="meter-slot" :class="{ enabled: meter.enabled }">
            <label class="meter-option meter-toggle"><input v-model="meter.enabled" type="checkbox"><span>Enable meter {{ slot + 1 }}</span></label>
            <div v-if="meter.enabled" class="meter-slot-settings">
              <div class="configuration-form meter-fields">
                <label class="configuration-field"><span>Name in Home Assistant</span><input v-model="meter.name" maxlength="48" required :placeholder="slot === 0 ? 'Schuur feeder' : 'Heat pump'"></label>
                <label class="configuration-field"><span>Modbus address</span><input v-model.number="meter.address" type="number" min="1" max="247" step="1" required></label>
              </div>
              <label class="meter-option meter-direction"><input v-model="meter.reverse_power_direction" type="checkbox"><span>Reverse power direction<small>Reverses signed power and swaps import/export energy. Currents stay positive.</small></span></label>
            </div>
          </div>
        </div>
        <div v-if="enabled" class="configuration-subsection meter-bus">
          <div class="subsection-heading"><h3>Shared RS485 connection</h3></div>
          <p class="meter-help muted">Use a different address (1–247) for each meter. Match the serial settings on all physical meters.</p>
          <div class="meter-bus-controls">
            <label class="configuration-field"><span>Baud rate</span><select v-model.number="configuration.baud_rate"><option v-for="baud in [1200, 2400, 4800, 9600, 19200]" :key="baud" :value="baud">{{ baud }}</option></select></label>
            <details class="meter-advanced"><summary>Advanced serial settings</summary><div class="configuration-form">
              <label class="configuration-field"><span>Parity</span><select v-model="configuration.parity"><option value="none">None</option><option value="even">Even</option><option value="odd">Odd</option></select></label>
              <label class="configuration-field"><span>Stop bits</span><select v-model.number="configuration.stop_bits"><option :value="1">1</option><option :value="2">2</option></select></label>
            </div></details>
          </div>
        </div>
        <div class="button-row configuration-actions meter-actions"><button type="submit" class="primary" :disabled="!dirty">{{ saving ? 'Saving…' : 'Save meter settings' }}</button><button type="button" :disabled="!dirty" @click="load">Discard changes</button></div>
      </fieldset>
    </form>
    <p v-if="result" class="action-result" role="status">{{ result }}</p>
    <div v-if="status.configuration.meters.some(meter => meter.enabled)" class="configuration-subsection meter-readings">
      <h3>Live readings</h3>
      <div class="meter-slots">
        <template v-for="(meter, slot) in status.meters" :key="slot">
          <div v-if="status.configuration.meters[slot]?.enabled" class="meter-reading">
            <div class="meter-reading-heading"><h4>{{ status.configuration.meters[slot]?.name }}</h4><span class="meter-state" :class="{ available: connected && meter.available }">{{ connected && meter.available ? 'Available' : 'Unavailable' }}</span></div>
            <dl v-if="connected && meter.available && meter.electrical" class="meter-values">
              <div><dt>Total power</dt><dd>{{ meter.electrical.total_power_w.toFixed(0) }} W</dd></div>
              <div><dt>L1 current</dt><dd>{{ meter.electrical.current_l1_a.toFixed(2) }} A</dd></div>
              <div><dt>L2 current</dt><dd>{{ meter.electrical.current_l2_a.toFixed(2) }} A</dd></div>
              <div><dt>L3 current</dt><dd>{{ meter.electrical.current_l3_a.toFixed(2) }} A</dd></div>
            </dl>
            <dl v-if="connected && meter.energy_fresh && meter.energy" class="meter-values meter-energy">
              <div><dt>Import energy</dt><dd>{{ meter.energy.import_kwh.toFixed(3) }} kWh</dd></div>
              <div><dt>Export energy</dt><dd>{{ meter.energy.export_kwh.toFixed(3) }} kWh</dd></div>
            </dl>
            <details class="meter-diagnostics"><summary>Diagnostics</summary>
              <dl class="meter-diagnostic-values">
                <div><dt>Last successful read</dt><dd>{{ age(meter.last_success_age_ms) }}</dd></div>
                <div><dt>Failed reads</dt><dd>{{ meter.failed_reads }}</dd></div>
                <div v-if="meter.serial_number !== undefined"><dt>Serial number</dt><dd>{{ meter.serial_number }}</dd></div>
                <div v-if="meter.meter_code !== undefined"><dt>Model code</dt><dd>{{ meter.meter_code }}</dd></div>
                <div v-if="meter.firmware_version"><dt>Firmware</dt><dd>{{ meter.firmware_version }}</dd></div>
              </dl>
            </details>
            <p v-if="meter.last_error" class="meter-help muted">Last read error: {{ meter.last_error }}</p>
          </div>
        </template>
      </div>
    </div>
  </section>
</template>

<style scoped>
.meter-settings { border: 0; padding: 0; margin: 0; min-width: 0; }
.energy-meters-panel .panel-heading { margin-bottom: .75rem; }
.energy-meters-panel .panel-heading p { max-width: none; margin: .35rem 0 0; }
.meter-overview { display: flex; align-items: center; flex-wrap: wrap; gap: .5rem 1rem; margin-bottom: 1rem; font-size: .85rem; }
.meter-state { display: inline-block; flex-shrink: 0; padding: .2rem .55rem; border-radius: .35rem; background: #edf1f5; color: #526174; font-size: .8rem; font-weight: 650; }
.meter-state.available { background: #e4f5ec; color: #17613b; }
.meter-slots { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: .75rem; align-items: start; }
.meter-slot { border: 1px solid #dbe2eb; border-radius: .5rem; background: #f8fafc; overflow: hidden; }
.meter-slot.enabled { background: #fff; }
.meter-option { display: flex; align-items: flex-start; gap: .55rem; font-size: .85rem; font-weight: 650; }
.meter-option input { flex-shrink: 0; width: 1.1rem; height: 1.1rem; margin: .05rem 0 0; }
.meter-toggle { padding: .8rem; cursor: pointer; }
.meter-slot-settings { padding: 0 .8rem .8rem; }
.meter-fields { grid-template-columns: minmax(0, 1fr) 8rem; gap: .75rem; }
.energy-meters-panel .configuration-field > span { min-height: 0; }
.meter-direction { margin-top: .85rem; }
.meter-direction small { display: block; margin-top: .25rem; font-weight: 400; line-height: 1.4; color: #64748b; }
.meter-bus { margin-top: 1rem; }
.meter-help { font-size: .8rem; line-height: 1.4; margin: 0 0 .75rem; }
.meter-bus-controls { display: grid; grid-template-columns: 12rem minmax(0, 1fr); gap: 1rem; align-items: start; }
summary { cursor: pointer; font-size: .85rem; }
details[open] > summary { margin-bottom: .75rem; }
.meter-advanced { padding-top: .15rem; }
.meter-advanced .configuration-form { gap: .75rem; }
.meter-actions { display: flex; flex-wrap: wrap; gap: .5rem; }
.meter-readings { margin-top: 1rem; }
.meter-readings > h3 { margin: 0 0 .75rem; font-size: 1rem; }
.meter-reading { min-width: 0; border: 1px solid #dbe2eb; border-radius: .5rem; padding: .8rem; }
.meter-reading-heading { display: flex; align-items: baseline; justify-content: space-between; gap: .5rem; margin-bottom: .75rem; }
.meter-reading-heading h4 { margin: 0; font-size: .95rem; overflow-wrap: anywhere; }
.meter-values { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: .6rem 1rem; margin: 0; }
.meter-values dt { font-size: .8rem; color: #64748b; }
.meter-values dd { margin: .15rem 0 0; font-size: .95rem; font-weight: 650; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
.meter-energy { border-top: 1px solid #e3e9f0; padding-top: .75rem; margin-top: .75rem; }
.meter-diagnostics { margin-top: .75rem; color: #64748b; }
.meter-diagnostic-values { margin: 0; font-size: .8rem; }
.meter-diagnostic-values > div { display: flex; flex-wrap: wrap; justify-content: space-between; gap: .25rem .75rem; margin-top: .35rem; }
.meter-diagnostic-values dd { margin: 0; font-variant-numeric: tabular-nums; overflow-wrap: anywhere; }
.meter-reading > .meter-help { margin: .75rem 0 0; }
@media (max-width: 800px) { .meter-slots { grid-template-columns: minmax(0, 1fr); } }
@media (max-width: 560px) {
  .meter-fields, .meter-bus-controls { grid-template-columns: minmax(0, 1fr); }
  .meter-reading-heading { flex-wrap: wrap; }
}
</style>
