<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import ServiceView from './ServiceView.vue'
import { bmsFaultNames, bmsStateName, cellVoltageV, currentA, hvReasonNames, hvStateName, icCelsius, ntcCelsius, setBits, socPercent } from '@/shared/model'
import { csvHeader, csvRow, downloadCsv } from '@/shared/csv'
import type { BmsTransport, Capabilities, GatewayStatus, Snapshot, Status } from '@/transports/Transport'

const props = defineProps<{ snapshot: Snapshot | null; status: Status | null; transport: BmsTransport; capabilities: Capabilities; connected: boolean; gateway?: GatewayStatus }>()
const lines = ref<string[]>([])
const logging = ref(false)
const fresh = computed(() => Boolean(props.snapshot && props.status?.measurements_fresh))
const activeFaults = computed(() => props.status ? setBits(props.status.bms_active_faults, bmsFaultNames) : [])
const hvFaults = computed(() => props.status ? setBits(props.status.hv_active_faults, hvReasonNames) : [])
const measurement = (value: string) => fresh.value ? value : '—'

function appendSnapshot(snapshot: Snapshot): void {
  if (!snapshot.status.measurements_fresh) return
  if (lines.value.length === 0) lines.value = [csvHeader(snapshot).join(',')]
  lines.value.push(csvRow(snapshot).join(','))
}
function startLogging(): void { if (props.snapshot && fresh.value) { lines.value = []; appendSnapshot(props.snapshot); logging.value = true } }
function stopLogging(): void { logging.value = false }
watch(() => props.snapshot, snapshot => { if (logging.value && snapshot) appendSnapshot(snapshot) })
</script>

<template>
  <main class="dashboard">
    <section class="dashboard-top">
      <div class="metrics-grid">
        <article class="metric"><span>BMS state</span><b>{{ status ? bmsStateName(status.bms_state) : 'Waiting' }}</b></article>
        <article class="metric"><span>HV state</span><b>{{ status ? hvStateName(status.hv_state) : 'Waiting' }}</b></article>
        <article class="metric"><span>Run request</span><b>{{ status?.run_request ? 'Requested' : 'Off' }}</b></article>
        <article class="metric"><span>Pack voltage</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.pack_voltage_uV).toFixed(3)} V`) : '—' }}</b></article>
        <article class="metric"><span>Pack current</span><b>{{ snapshot ? measurement(`${currentA(snapshot.pack.pack_current_raw).toFixed(2)} A`) : '—' }}</b></article>
        <article class="metric"><span>State of charge</span><b>{{ snapshot ? measurement(`${socPercent(snapshot.pack.soc_raw).toFixed(1)} %`) : '—' }}</b></article>
        <article class="metric"><span>Cell range</span><b>{{ snapshot ? measurement(`${cellVoltageV(snapshot.pack.min_cell_uV).toFixed(3)}–${cellVoltageV(snapshot.pack.max_cell_uV).toFixed(3)} V`) : '—' }}</b></article>
        <article class="metric"><span>Temperature range</span><b>{{ snapshot ? measurement(`${ntcCelsius(snapshot.pack.min_ntc_raw).toFixed(1)}–${ntcCelsius(snapshot.pack.max_ntc_raw).toFixed(1)} °C`) : '—' }}</b></article>
      </div>
      <section class="panel faults"><div class="panel-heading"><div><h2>Faults and HV reasons</h2></div></div><div><h3>Active BMS faults</h3><p>{{ activeFaults.join(', ') || 'None reported' }}</p><h3>Latched BMS faults</h3><p>{{ status ? setBits(status.bms_latched_faults, bmsFaultNames).join(', ') || 'None reported' : 'Waiting for status' }}</p><h3>Active HV reasons</h3><p>{{ hvFaults.join(', ') || 'None reported' }}</p><h3>Latched HV reasons</h3><p>{{ status ? setBits(status.hv_latched_faults, hvReasonNames).join(', ') || 'None reported' : 'Waiting for status' }}</p></div></section>
    </section>

    <section class="panel"><div class="panel-heading"><div><h2>Cell voltages and balancing</h2></div><p v-if="!fresh">Values remain hidden until a complete fresh snapshot arrives.</p></div><div v-if="fresh && snapshot" class="table-scroll"><table><thead><tr><th>Slave</th><th v-for="index in 12" :key="index">C{{ index }}</th></tr></thead><tbody><tr v-for="cell in snapshot.cells" :key="cell.slave_index"><th>Slave {{ cell.slave_index }}</th><td v-for="(value, index) in cell.cell_voltage_uV" :key="index" :class="{ balancing: (cell.balance_mask & (1 << index)) !== 0 }">{{ cellVoltageV(value).toFixed(4) }} V<span v-if="(cell.balance_mask & (1 << index)) !== 0"> balancing</span></td></tr></tbody></table></div></section>

    <section class="panel"><div class="panel-heading"><div><h2>Temperatures</h2></div></div><div v-if="fresh && snapshot" class="table-scroll"><table><thead><tr><th>Slave</th><th>NTC 0</th><th>NTC 1</th><th>NTC 2</th><th>NTC 3</th><th>IC</th></tr></thead><tbody><tr v-for="temperature in snapshot.temperatures" :key="temperature.slave_index"><th>Slave {{ temperature.slave_index }}</th><td v-for="(value, index) in temperature.ntc_raw" :key="index">{{ ntcCelsius(value).toFixed(1) }} °C</td><td>{{ icCelsius(temperature.ic_temp_raw).toFixed(1) }} °C</td></tr></tbody></table></div></section>

    <section class="panel logging-panel"><div class="panel-heading"><div><h2>CSV logging</h2></div><p>No data is sent to the Gateway or stored on the BMS.</p></div><div class="button-row"><button class="primary" :disabled="!fresh || logging" @click="startLogging">Start logging</button><button :disabled="!logging" @click="stopLogging">Stop logging</button><button :disabled="lines.length < 2" @click="downloadCsv(lines)">Download {{ Math.max(0, lines.length - 1) }} rows</button></div></section>

    <ServiceView :transport="transport" :capabilities="capabilities" :connected="connected" :run-requested="status?.run_request ?? false" :gateway="gateway" />
  </main>
</template>
