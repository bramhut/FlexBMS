// Development preview: actual view components with sample data and mock services.
// Never imported by the production app. No network or battery connection.
import { createApp, h, ref } from 'vue'
import ConnectionHeader from '../../src/app/ConnectionHeader.vue'
import DashboardView from '../../src/bms/DashboardView.vue'
import DiagnosticsView from '../../src/bms/DiagnosticsView.vue'
import ConfigurationView from '../../src/bms/ConfigurationView.vue'
import FirmwareView from '../../src/bms/FirmwareView.vue'
import RecentChangesDrawer from '../../src/bms/RecentChangesDrawer.vue'
import '../../src/assets/index.css'
import policyBytes from '../fixtures/safety-policy.json'
import { unavailableCapabilities, type BmsTransport, type EnergyMeterConfiguration, type GatewayStatus,
  type RuntimeConfiguration, type RecordedControllerEvent, type ServiceName, type Snapshot, type Status } from '../../src/transports/Transport'

const active = ref('dashboard'), connected = ref(true), permitted = ref(true)
const heatmap = ref(false), delta = ref(true), drawer = ref(false)
const recentEvents = ref<RecordedControllerEvent[]>([])
const requests = ref<string[]>([])
const runtimeConfiguration = ref<RuntimeConfiguration>({ reason: 'valid', expected_version: 5, stored_version: 5,
  slave_count: 8, current_sense_slave: 1, shunt_resistance_uohm: 375, battery_capacity_mah: 314000,
  invert_current: false, balance_enabled: true, startup_diagnostics: true,
  self_discharge_tenth_percent_per_30_days: 10, cold_allowance_deci_c: 30 })
const unixTime = Math.floor(Date.now() / 1000)
const status = ref<Status>({ bms_state: 1, hv_state: 0, flags: 1, slave_count: 8, bms_active_errors: 0,
  bms_latched_errors: 0, hv_active_errors: 0, hv_latched_errors: 0, warnings: 0, uptime_ms: 1200000,
  measurements_fresh: true, run_request: false, balancing_enabled: true, soc_valid: true, current_sensing_enabled: true,
  soc_last_calibration_unix_s: unixTime - 25200,
  soc_calibration: { valid: true, unix_time_s: unixTime - 25200, previous_unix_time_s: unixTime - 106400,
    pre_soc_raw: 43675, qualifying_dwell_ms: 300000, self_discharge_rate_tenth_percent_per_30_days: 10,
    self_discharge_soc_valid: true, self_discharge_equivalent_current_uA: 4361,
    self_discharge_accumulated_since_calibration_uAh: 1371, self_discharge_interval_complete: false,
    last_calibration_self_discharge_mAh: 98, last_calibration_self_discharge_valid: true, last_calibration_self_discharge_complete: true } })
const snapshot = ref<Snapshot>({ status: status.value,
  pack: { pack_voltage_uV: 315880000, pack_current_uA: 0, soc_raw: 31260, min_cell_uV: 3289000, max_cell_uV: 3292000,
    min_ntc_raw: 24139, max_ntc_raw: 24848, min_ic_raw: 30595, max_ic_raw: 31555 },
  energy: { valid: true, charged_energy_uWh: '1253185000000', discharged_energy_uWh: '1211738000000' },
  hv_voltages: { valid: true, bat_plus_uV: 315520000, load_plus_uV: 0 },
  cells: Array.from({ length: 8 }, (_, slave_index) => ({ slave_index, balance_mask: 0,
    cell_voltage_uV: Array.from({ length: 12 }, (_, cell) => 3289000 + ((slave_index + cell) % 4) * 1000),
    balancing_mAh: Array.from({ length: 12 }, (_, cell) => slave_index * 20 + cell * 3) })),
  temperatures: Array.from({ length: 8 }, (_, slave_index) => ({ slave_index, ntc_raw: [24576, 24468, 24848, 24139], ic_temp_raw: 31095 + slave_index * 55 })),
  goodwe_can: { schema_version: 1, protocol: 1, request_45a_enabled: true, compatibility_460_enabled: true,
    transmit_cycles: 1200, snapshot_unavailable_cycles: 1, transmit_failures: 0, last_transmit_failure_ms: 0,
    last_transmit_failure_id: 0, reported_458_current_deci_a: 0, reported_458_voltage_deci_v: 3159,
    transmit_error_count: 0, receive_error_count: 0, error_logging_count: 0, protocol_last_error_code: 0,
    protocol_activity: 8, error_passive: false, warning: false, bus_off: false, hal_error_code: 0, transmit_fifo_free_level: 3,
    transmit_frames: [0x453, 0x455, 0x456, 0x457, 0x458, 0x45a, 0x460].map(id => ({ id, success_count: 1200, last_success_ms: 1199750 })),
    receive_frames: [{ id: 0x425, count: 1200, last_seen_ms: 1199750, length: 8, data: [0x57, 0x0c, 0, 0, 0, 1, 1, 0] }] },
})
const gateway = ref<GatewayStatus>({ gateway_version: '0.2.0+preview', gateway_build_id: 'local-preview',
  gateway_uptime_ms: 1186000, gateway_partition: 'ota_1', wifi_state: 'connected', wifi_ssid: 'Home network',
  wifi_connection: { bssid: '02:00:00:00:00:01', channel: 6, rssi_dbm: -54 }, setup_ap: { active: false },
  uart_state: 'healthy', mqtt_state: 'connected', mqtt: { configured: true, host: '192.168.1.50', port: 1883, username: 'mqtt' },
  time_sync: { state: 'synchronized' }, diagnostic_log: { available: false, bytes: 0 },
  firmware_update: { phase: 'idle', target: 'gateway', stage: 'idle', received_bytes: 0, expected_bytes: 0, progress_bytes: 0, version: '', detail: '' },
  energy_meters: { configuration: { baud_rate: 9600, parity: 'none', stop_bits: 1,
    meters: [{ enabled: true, name: 'Schuur feeder', address: 1, reverse_power_direction: false },
      { enabled: true, name: 'Heat pump', address: 2, reverse_power_direction: false }] },
    stored_configuration_valid: true, bus_state: 'ready', bus_error: 0,
    meters: Array.from({ length: 2 }, (_, slot) => ({ available: true, energy_fresh: true, failed_reads: 0, last_error: 0,
      last_success_age_ms: 250, serial_number: 12345678 + slot, meter_code: 72, firmware_version: '1.2',
      electrical: { current_l1_a: 8.25, current_l2_a: 12.5, current_l3_a: 3.75,
        power_l1_w: 1897, power_l2_w: 2875, power_l3_w: 862, total_power_w: 5634, sample_age_ms: 250, failed_reads: 0 },
      energy: { import_kwh: 1234.567, export_kwh: 89.012 } })) },
})
function record(service: string, args: unknown = {}) { requests.value = [`${service}: ${JSON.stringify(args)}`, ...requests.value].slice(0, 10) }
const transport = {
  request: async (service: ServiceName, args: Record<string, unknown>) => {
    record(service, args)
    let data: Record<string, unknown> = {}
    if (service === 'get_config') data = { ...runtimeConfiguration.value }
    if (service === 'get_device_info') data = { firmware_version_packed: 512 }
    if (service === 'get_safety_limits') data = { safety_limits_payload: policyBytes }
    if (service === 'read_register') data = { value: 0x1234 }
    if (service === 'set_config') {
      runtimeConfiguration.value = { ...runtimeConfiguration.value, ...args, reason: 'valid' }
      status.value = { ...status.value, slave_count: runtimeConfiguration.value.slave_count, current_sensing_enabled: runtimeConfiguration.value.current_sense_slave > 0 }
    }
    return { request_id: 'preview', service, result: 'ok', data }
  },
  scanWifi: async () => ({ request_id: 'preview', result: 'ok', networks: [{ ssid: 'Home network', rssi: -54, secure: true }] }),
  configureWifi: async (ssid: string) => {
    record('configure_wifi', { ssid })
    gateway.value = { ...gateway.value, wifi_ssid: ssid }
    return { request_id: 'preview', result: 'accepted' }
  },
  configureMqtt: async (host: string, port: number, username: string) => {
    record('configure_mqtt', { host, port, username })
    gateway.value = { ...gateway.value, mqtt: { configured: true, host, port, username } }
    return { request_id: 'preview', result: 'accepted' }
  },
  configureEnergyMeters: async (configuration: EnergyMeterConfiguration) => {
    record('configure_energy_meters', configuration)
    gateway.value = { ...gateway.value, energy_meters: { ...gateway.value.energy_meters!,
      configuration: structuredClone(configuration), bus_state: configuration.meters.some(m => m.enabled) ? 'ready' : 'disabled' } }
    return { request_id: 'preview', result: 'accepted' }
  },
} as unknown as BmsTransport

// Firmware previews parse local files; installation is explicitly blocked.
const originalFetch = window.fetch.bind(window)
window.fetch = (input, init) => String(input).startsWith('/api/')
  ? Promise.resolve(new Response(JSON.stringify({ result: 'denied', detail: 'Local preview only; installation is disabled.' }), { status: 403 }))
  : originalFetch(input, init)

createApp({ setup: () => () => {
  const capabilities = Object.fromEntries(Object.keys(unavailableCapabilities()).map(key => [key, permitted.value])) as ReturnType<typeof unavailableCapabilities>
  const common = { connected: connected.value, transport, capabilities }
  const sampleTime = performance.now()
  return h('div', [
    h(ConnectionHeader, { label: 'Local preview', message: 'Sample data · no battery connection', tone: connected.value ? 'healthy' : 'problem', allowManualConnect: false }),
    h('nav', { 'aria-label': 'Main navigation' }, ['dashboard', 'diagnostics', 'configuration', 'firmware'].map((tab, index) => h('button',
      { class: { active: active.value === tab }, onClick: () => { active.value = tab } }, ['BMS dashboard', 'Diagnostics', 'Configuration', 'Firmware'][index]))),
    active.value === 'dashboard' ? h(DashboardView, { ...common, snapshot: snapshot.value, status: status.value, gateway: gateway.value,
      recentEvents: recentEvents.value, diagnosticReports: [], deviceTimeUnixS: unixTime, deviceTimeSampledAt: Date.now(),
      bmsUptimeSampledAt: sampleTime, gatewayUptimeSampledAt: sampleTime, heatmapEnabled: heatmap.value, deltaViewEnabled: delta.value,
      'onUpdate:heatmapEnabled': (value: boolean) => { heatmap.value = value }, 'onUpdate:deltaViewEnabled': (value: boolean) => { delta.value = value }, onShowAll: () => { drawer.value = true } }) :
    active.value === 'diagnostics' ? h(DiagnosticsView, { ...common, snapshot: snapshot.value, status: status.value, diagnosticReports: [] }) :
    active.value === 'configuration' ? h(ConfigurationView, { ...common, gateway: gateway.value }) :
    h(FirmwareView, { ...common, gateway: gateway.value }),
    h(RecentChangesDrawer, { events: recentEvents.value, open: drawer.value, onClose: () => { drawer.value = false } }),
    h('footer', { class: 'stack preview-controls' }, [h('details', { class: 'panel' }, [h('summary', 'Preview controls'),
      h('div', { class: 'button-row', style: 'margin-top:.75rem' }, [
        h('button', { onClick: () => { connected.value = !connected.value } }, 'Toggle connection'),
        h('button', { onClick: () => { permitted.value = !permitted.value } }, 'Toggle permissions'),
        h('button', { onClick: () => { gateway.value = JSON.parse(JSON.stringify(gateway.value)) } }, 'Refresh Gateway status'),
        h('button', { onClick: () => { runtimeConfiguration.value = { ...runtimeConfiguration.value, reason: 'blank' }; connected.value = false } }, 'Simulate blank configuration'),
        h('button', { onClick: () => { status.value = { ...status.value, bms_active_errors: status.value.bms_active_errors ? 0 : 1 } } }, 'Toggle sample fault'),
        h('button', { onClick: () => { recentEvents.value = recentEvents.value.length ? [] : [{ event_id: 1, value: 1, observed_at_ms: Date.now() }] } }, 'Toggle sample activity'),
        h('button', { onClick: () => { snapshot.value = { ...snapshot.value } } }, 'Add sample snapshot'),
      ]), h('p', 'Settings and requests affect only this preview. Firmware installation is disabled.'),
      h('pre', { style: 'white-space:pre-wrap;overflow-wrap:anywhere;font-size:.8rem' }, requests.value.join('\n'))])]),
  ])
} }).mount('#app')
