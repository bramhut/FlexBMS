// Development fixture only; never imported by the application entry.
import { createApp, h, ref } from 'vue'
import EnergyMetersConfiguration from '../../src/bms/EnergyMetersConfiguration.vue'
import '../../src/assets/index.css'
import type { BmsTransport, EnergyMeterConfiguration, EnergyMeterStatus } from '../../src/transports/Transport'

const connected = ref(true), allowed = ref(true), saves = ref(0)
const status = ref<EnergyMeterStatus>({
  configuration: { baud_rate: 9600, parity: 'none', stop_bits: 1, meters: [
    { enabled: false, name: 'Meter 1', address: 1, reverse_power_direction: false },
    { enabled: false, name: 'Meter 2', address: 2, reverse_power_direction: false },
  ] },
  stored_configuration_valid: true, bus_state: 'disabled', bus_error: 0,
  meters: Array.from({ length: 2 }, () => ({ available: false, energy_fresh: false, failed_reads: 0, last_error: 0, last_success_age_ms: null })),
})
const transport = { configureEnergyMeters: async (configuration: EnergyMeterConfiguration) => {
  saves.value++
  status.value = { ...status.value, configuration: structuredClone(configuration), bus_state: configuration.meters.some(m => m.enabled) ? 'ready' : 'disabled' }
  return { request_id: 'fixture', result: 'accepted' }
} } as unknown as BmsTransport
createApp({ setup: () => () => h('main', { style: 'padding:1rem;max-width:1100px;margin:auto' }, [
  h('div', { class: 'panel' }, [
    h('strong', 'Local test data · no battery connection'),
    h('p', `Saved requests: ${saves.value}`),
    h('button', { onClick: () => { status.value = structuredClone({ ...status.value, configuration: { ...status.value.configuration, meters: status.value.configuration.meters.map(m => ({ ...m })) }, meters: status.value.meters.map(m => ({ ...m })) }) } }, 'Refresh live status'),
    h('button', { onClick: () => { connected.value = !connected.value } }, 'Toggle connection'),
    h('button', { onClick: () => { allowed.value = !allowed.value } }, 'Toggle station permission'),
  ]),
  h(EnergyMetersConfiguration, { transport, connected: connected.value, allowed: allowed.value, status: status.value }),
]) }).mount('#app')
