// Development-only fixture: no network/serial requests and never included in the app entry.
import { createApp, h, ref } from 'vue'
import DiagnosticsView from '../../src/bms/DiagnosticsView.vue'
import '../../src/assets/index.css'
import bytes from '../fixtures/safety-policy.json'
import { unavailableCapabilities, type BmsTransport, type Status, type Snapshot } from '../../src/transports/Transport'
const connected = ref(true)
const unsupported = ref(false)
const requests = ref(0)
const status = ref<Status>({ bms_state: 2, hv_state: 4, flags: 0, slave_count: 8, bms_active_errors: 0, bms_latched_errors: 0, hv_active_errors: 0, hv_latched_errors: 0, warnings: 0, uptime_ms: 300000, measurements_fresh: true, run_request: true, balancing_enabled: true, soc_valid: true, current_sensing_enabled: true, soc_calibration: { valid: true, unix_time_s: 1791291600, previous_unix_time_s: 1791205200, pre_soc_raw: 43392, qualifying_dwell_ms: 300000, self_discharge_rate_tenth_percent_per_30_days: 10, self_discharge_soc_valid: true, self_discharge_equivalent_current_uA: 4361, self_discharge_accumulated_since_calibration_uAh: 12000, self_discharge_interval_complete: true } })
const snapshot = { status: status.value, cells: Array.from({ length: 8 }, (_, i) => ({ slave_index: i, balance_mask: 0, cell_voltage_uV: Array(12).fill(3300000), balancing_mAh: Array.from({ length: 12 }, (_, j) => i*20+j*3) })), temperatures: [], hv_voltages: { valid: true, bat_plus_uV: 316000000, load_plus_uV: 315000000 } } as unknown as Snapshot
const transport = { request: async (service: string) => { requests.value++; await new Promise(resolve => setTimeout(resolve, 100)); return { service, result: unsupported.value ? 'invalid' : 'ok', data: { safety_limits_payload: bytes } } } } as unknown as BmsTransport
createApp({ setup: () => () => h('div', [
  h('div', { class: 'panel', style: 'margin:1rem' }, [
    h('b', 'Local test data · no battery connection'),
    h('button', { onClick: () => { connected.value = !connected.value } }, connected.value ? 'Disconnect fixture' : 'Connect fixture'),
    h('button', { onClick: () => { unsupported.value = !unsupported.value } }, unsupported.value ? 'Use supported firmware' : 'Use unsupported firmware'),
    h('button', { onClick: () => { status.value = { ...status.value, uptime_ms: status.value.uptime_ms > 1000 ? 10 : 300000 } } }, 'Simulate controller restart'),
    h('span', ` Policy requests: ${requests.value}`),
  ]),
  h(DiagnosticsView, { snapshot, status: status.value, transport, capabilities: unavailableCapabilities(), connected: connected.value, diagnosticReports: [] }),
]) }).mount('#app')
