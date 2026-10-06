<script setup lang="ts">
import { computed, ref, watch, onBeforeUnmount } from 'vue'
import { decodeSafetyPolicy, type SafetyPolicy } from '../shared/safetyPolicy'
import { createUptimeTracker, resetUptimeTracker, sampleUptime } from '../shared/time'
import { serviceResultLabel } from '../shared/service'
import type { BmsTransport, Status } from '../transports/Transport'

const props = defineProps<{ active: boolean; connected: boolean; transport: BmsTransport; status: Status | null }>()
const policy = ref<SafetyPolicy | null>(null)
const busy = ref(false)
const error = ref('')
const uptime = createUptimeTracker()
let generation = 0
const voltage = (mv: number) => `${(mv / 1000).toFixed(3)} V`
const temp = (deci: number) => `${(deci / 10).toFixed(1)}°C`
const amps = (ma: number) => `${(ma / 1000).toFixed(2)} A`
const rate = (permille: number) => `${(permille / 1000).toFixed(3)}C · ${((policy.value?.capacity_mah ?? 0) * permille / 1e6).toFixed(2)} A`
const version = computed(() => { const v = policy.value?.firmware_version ?? 0; return `${v & 255}.${(v >>> 8) & 255}.${(v >>> 16) & 255}` })
const coldRows = computed(() => {
  const p = policy.value
  return p ? [[p.cold_t0_deci_c, p.cold_c0_permille], [p.cold_t1_deci_c, p.cold_c1_permille], [p.cold_t2_deci_c, p.cold_c2_permille], [p.cold_t3_deci_c, p.cold_c3_permille]] : []
})
async function refresh(): Promise<void> {
  if (!props.connected || busy.value) return
  const token = generation
  busy.value = true
  policy.value = null
  error.value = ''
  try {
    const response = await props.transport.request('get_safety_limits', {})
    if (token !== generation) return
    if (response.result !== 'ok') {
      error.value = response.result === 'invalid' ? 'Unavailable: this firmware does not support reporting safety limits.' : `Unavailable: ${serviceResultLabel(response.result)}. Retry when the controller is reachable.`
      return
    }
    policy.value = decodeSafetyPolicy(response.data?.safety_limits_payload)
    if (!policy.value) error.value = 'Unavailable: unsupported or malformed safety-policy response.'
  } catch {
    if (token === generation) error.value = 'Unavailable: safety limits could not be read.'
  } finally {
    if (token === generation) busy.value = false
  }
}
function invalidate(): void {
  ++generation; busy.value = false; policy.value = null; error.value = ''
}
watch(() => [props.connected, props.transport] as const, () => {
  invalidate(); resetUptimeTracker(uptime)
  if (props.connected && props.active) void refresh()
})
watch(() => props.status?.uptime_ms, value => {
  if (value !== undefined && sampleUptime(uptime, value, performance.now()) === 'restart') {
    invalidate()
    if (props.connected && props.active) void refresh()
  }
}, { immediate: true })
watch(() => props.active, active => { if (active && props.connected && !policy.value && !error.value) void refresh() }, { immediate: true })
onBeforeUnmount(invalidate)
</script>

<template>
  <div class="diagnostics-content">
    <section class="panel">
      <div class="panel-heading"><div><h2>Safety limits</h2><p>Read-only policy reported by the connected STM32. No assumed defaults.</p></div><button :disabled="!connected || busy" @click="refresh">{{ busy ? 'Reading…' : 'Refresh limits' }}</button></div>
      <p v-if="!connected" class="muted">Unavailable while disconnected.</p>
      <p v-else-if="error" role="status" class="warning-text">{{ error }}</p>
      <p v-else-if="!policy" class="muted">{{ busy ? 'Reading STM32 safety policy…' : 'No policy loaded.' }}</p>
      <template v-else>
        <p class="safety-note">STM32 {{ version }} · {{ (policy.capacity_mah / 1000).toFixed(3) }} Ah · These are policy settings, not live current allowances or hardware register readback.</p>
        <p v-if="!policy.configuration_valid" class="warning-text">No active valid configuration. The reported runtime values are defaults, not a confirmed operating configuration.</p>
        <p class="safety-note">Derating reduces the inverter allowance. Inhibition sets it to zero and recovers automatically. A hard fault opens the HV path and requires acknowledgement after the cause clears.</p>
      </template>
    </section>
    <template v-if="policy && connected">
      <section class="panel">
        <h2>Cell voltage</h2>
        <table class="safety-table"><thead><tr><th>Direction / measurement</th><th>Starts limiting</th><th>Stops operation</th></tr></thead><tbody>
          <tr><th>Charge · highest cell</th><td>Above {{ voltage(policy.charge_derate_mv) }}</td><td>At {{ voltage(policy.charge_stop_mv) }} · zero charge allowance</td></tr>
          <tr><th>Discharge · lowest cell</th><td>Below {{ voltage(policy.discharge_derate_mv) }}</td><td>At {{ voltage(policy.discharge_stop_mv) }} · zero discharge allowance</td></tr>
        </tbody></table>
        <p class="safety-note">Linear derating between endpoints. Recovery follows the same curve with a {{ policy.recovery_permille_per_s / 10 }}% of system ceiling per second rise limit; reductions are immediate. Charge/discharge pack-voltage targets use the respective stop/derating endpoint multiplied by the cell count.</p>
        <details><summary>Independent hard voltage faults</summary>
          <p>Overvoltage: {{ voltage(policy.cell_ov_mv) }} nominal · programmed threshold {{ (policy.cell_ov_programmed_uv / 1e6).toFixed(4) }} V.<br>Undervoltage: {{ voltage(policy.cell_uv_mv) }} nominal · programmed threshold {{ (policy.cell_uv_programmed_uv / 1e6).toFixed(4) }} V.</p>
          <p class="safety-note">Programmed values include BCC threshold quantization, calculated from the initialization settings; they are not a register readback. A hardware fault latches HV safe-off.</p>
        </details>
      </section>
      <section class="panel">
        <h2>Temperature</h2>
        <table class="safety-table"><thead><tr><th>Measurement</th><th>Inhibition</th><th>Recovery</th></tr></thead><tbody>
          <tr><th>Cold charge · lowest busbar NTC minus {{ temp(policy.cold_allowance_deci_c) }}</th><td>Effective temperature ≤ {{ temp(policy.cold_stop_deci_c) }}</td><td>Effective temperature &gt; {{ temp(policy.cold_recover_deci_c) }}</td></tr>
          <tr><th>Hot charge &amp; discharge · highest NTC, no subtraction</th><td>Zero allowance at ≥ {{ temp(policy.hot_stop_deci_c) }}</td><td>Below {{ temp(policy.hot_recover_deci_c) }}</td></tr>
        </tbody></table>
        <details><summary>Temperature derating curves</summary>
          <p class="safety-note">Cold-charge points use effective temperature, with linear interpolation. The first point is the limiting value just above the stop temperature, not permission to charge at that temperature. Boot and invalid measurements require the recovery thresholds.</p>
          <table class="safety-table"><thead><tr><th>Effective cold temperature</th><th>Cell charge capability</th></tr></thead><tbody><tr v-for="(row, index) in coldRows" :key="index"><td>{{ index === 0 ? 'Just above ' : index === 3 ? 'At / above ' : '' }}{{ temp(row[0]) }}</td><td>{{ rate(row[1]) }}</td></tr></tbody></table>
          <p>Hot charge/discharge capability: {{ rate(policy.hot_c_permille) }} through {{ temp(policy.hot_start_deci_c) }}, then linear reduction to zero at {{ temp(policy.hot_stop_deci_c) }}.</p>
          <p class="safety-note">C-rates are capacity-based cell capability, not the final inverter allowance. The lowest of thermal, voltage and system limits applies. No cold-discharge derating is configured.</p>
        </details>
        <details><summary>Independent hard thermal faults</summary><p>Busbar NTC overtemperature: {{ temp(policy.cell_ot_deci_c) }} nominal BCC threshold. Monitor IC: hardware thermal-shutdown fault; no additional software IC-temperature cutoff.</p><p class="safety-note">Both use latched HV safe-off. NTC thresholds depend on the configured thermistor conversion and hardware quantization.</p></details>
      </section>
      <section class="panel">
        <h2>Current and inverter enforcement</h2>
        <table class="safety-table"><thead><tr><th>Protection</th><th>Condition</th><th>Action</th></tr></thead><tbody>
          <tr><th>Absolute pack current</th><td>Charge &gt; {{ amps(policy.charge_max_ma) }}; discharge magnitude &gt; {{ amps(policy.discharge_max_ma) }}</td><td>Latched safe-off without an additional dwell</td></tr>
          <tr><th>Zero inverter allowance</th><td>Directional current &gt; {{ amps(policy.zero_tolerance_ma) }} for {{ policy.violation_dwell_ms / 1000 }} s</td><td>Latched safe-off</td></tr>
          <tr><th>Nonzero allowance L</th><td>Current &gt; L + max({{ amps(policy.limit_margin_ma) }}, {{ policy.limit_margin_permille / 10 }}% of L) for {{ policy.violation_dwell_ms / 1000 }} s</td><td>Latched safe-off</td></tr>
        </tbody></table>
        <details><summary>Enforcement details</summary><p class="safety-note">STM32 compares measured current to final downward-rounded 0.1 A CAN limits while HV is running. Each direction has its own timer. Missing samples do not accrue dwell; changing limits do not restart a continuing violation. A fault clears the run request. Acknowledgement does not restart operation. Cell measurement freshness window: {{ policy.communication_timeout_ms }} ms; communication protection remains independent. Current limits cannot be independently enforced without a configured working current sensor.</p></details>
      </section>
      <section class="panel">
        <h2>Balancing policy</h2><p class="safety-note">Operating policy, not a hard safety protection. Existing safety and freshness checks can interrupt balancing.</p>
        <table class="safety-table"><tbody>
          <tr><th>Minimum cell voltage</th><td>{{ voltage(policy.bal_min_mv) }}</td></tr>
          <tr><th>Difference from pack minimum</th><td>Start {{ policy.bal_start_delta_mv }} mV · stop {{ policy.bal_stop_delta_mv }} mV</td></tr>
          <tr><th>Qualifying pack current</th><td>{{ amps(policy.bal_min_ma) }} through {{ rate(policy.bal_max_c_permille) }} for {{ policy.bal_qualify_ms / 1000 }} s</td></tr>
          <tr><th>Pulse / simultaneous cells</th><td>{{ policy.bal_pulse_ms / 1000 }} s · at most {{ policy.bal_max_cells }} cells per slave</td></tr>
        </tbody></table><p class="safety-note">Leaving the qualifying current range prevents the next pulse; the current pulse may finish.</p>
      </section>
    </template>
  </div>
</template>
