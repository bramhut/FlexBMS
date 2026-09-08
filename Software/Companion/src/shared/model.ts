import type { Snapshot, Status } from '../transports/Transport'

export const cellVoltageV = (raw: number) => raw / 1_000_000
export const formatCellDifferenceMv = (microvolts: number): string => {
  if (!Number.isFinite(microvolts)) return 'Unavailable'
  return String(Math.round(Math.max(0, microvolts) / 1_000))
}
export const currentA = (raw: number) => raw / 64
export const powerW = (voltageUv: number, currentRaw: number) => cellVoltageV(voltageUv) * currentA(currentRaw)
export const socPercent = (raw: number) => 100 * (raw / 65535 * 3 - 1)
export const ntcCelsius = (raw: number) => raw / 65535 * 120 - 20
// The STM32 BCC driver exports IC temperature as centikelvin.
export const icCelsius = (raw: number) => raw / 100 - 273.15
const formatEnergyScaled = (microWh: bigint, divisor: bigint, decimals: bigint, unit: string): string => {
  const factor = 10n ** decimals
  const rounded = (microWh * factor + divisor / 2n) / divisor
  const whole = rounded / factor
  if (decimals === 0n) return `${whole} ${unit}`
  const fraction = (rounded % factor).toString().padStart(Number(decimals), '0')
  return `${whole}.${fraction} ${unit}`
}
export const formatEnergy = (microWh: string | number | bigint | undefined, valid = true): string => {
  if (!valid || microWh === undefined) return 'Unavailable'
  let value: bigint
  try { value = typeof microWh === 'bigint' ? microWh : BigInt(microWh) } catch { return 'Unavailable' }
  if (value < 1_000n) return `${value} µWh`
  if (value < 1_000_000n) return formatEnergyScaled(value, 1_000n, 2n, 'mWh')
  if (value < 1_000_000_000n) return formatEnergyScaled(value, 1_000_000n, 2n, 'Wh')
  return formatEnergyScaled(value, 1_000_000_000n, 3n, 'kWh')
}
export const formatPower = (watts: number | undefined, valid = true): string => {
  if (!valid || watts === undefined || !Number.isFinite(watts)) return 'Unavailable'
  if (Math.abs(watts) < 1_000) return `${Math.round(watts)} W`
  return `${(watts / 1_000).toPrecision(4)} kW`
}
export const isFresh = (snapshot: Snapshot | null) => snapshot !== null && snapshot.status.measurements_fresh
export const valueOrStale = (value: string, fresh: boolean) => fresh ? value : 'Stale'
export const bmsStateName = (value: number) => ['Starting', 'Ready', 'Running', 'Error', 'Critical'][value] ?? `Unknown (${value})`
export const hvStateName = (value: number) => ['Off', 'Self-test', 'Precharge', 'Contactor close', 'Run'][value] ?? `Unknown (${value})`
export const bmsFaultNames = ['CONFIGURATION_INVALID', 'SLAVE_UNAVAILABLE', 'BCC_DIAGNOSTICS', 'CELL_VOLTAGE_LIMIT', 'THERMAL_LIMIT', 'CURRENT_LIMIT', 'BCC_INTEGRITY', 'ADC_FAULT', 'BALANCING_HARDWARE_FAULT', 'BCC_COMMUNICATION', 'NO_CONFIG']
export const bccDiagnosticNames = ['ADC1 channel verification', 'OV/UV functional verification', 'OV/UV detection', 'Cell-terminal open/short detection', 'Cell-voltage channel verification', 'Cell contact resistance', 'Cell-terminal leakage', 'Current measurement', 'Shunt connection', 'GPIO over/under-temperature', 'GPIO open-terminal detection', 'Cell-balancing open-load detection']
export const bccDiagnosticStatusNames = ['Success', 'Parameter out of range', 'SPI communication failure', 'Communication timeout', 'Communication echo mismatch', 'Communication CRC error', 'Communication message-counter error', 'Empty communication response', 'Cannot enter diagnostic mode', 'Conversion data not ready']
export const hvReasonNames = ['HV_SENSOR_DIAGNOSTIC', 'BATTERY_VOLTAGE_MISMATCH', 'LOAD_SIDE_ENERGISED', 'PRECHARGE_TIMEOUT', 'PRECHARGE_VOLTAGE_LOST', 'CONTACTOR_VOLTAGE_LOST']
export const warningNames = ['WATCHDOG_RESET', 'STARTUP_DIAGNOSTICS_BYPASSED', 'BATTERY_VOLTAGE_MISMATCH_OFF', 'BCC_COMMUNICATION_RETRY']
export const warningDisplayNames = ['Watchdog reset', 'Startup diagnostics bypassed', 'Pack-voltage mismatch (HV off)', 'BCC communication retry']
export const setBits = (mask: number, labels: string[]) => labels.filter((label, bit) => (mask & (1 << bit)) !== 0 ? label : false)
export type BalancingDisplay = { label: string; state: 'waiting' | 'disabled' | 'idle' | 'active' | 'fault' }
export const balancingDisplay = (status: Status | null, cells: Snapshot['cells'] | null): BalancingDisplay => {
  const balancingFault = 1 << 8
  if (!status) return { label: 'Waiting', state: 'waiting' }
  if (((status.bms_active_errors | status.bms_latched_errors) & balancingFault) !== 0) return { label: 'Fault', state: 'fault' }
  if (!status.balancing_enabled) return { label: 'Disabled', state: 'disabled' }
  if (!status.measurements_fresh || cells === null) return { label: 'Waiting', state: 'waiting' }

  let activeCells = 0
  for (const cell of cells) {
    let mask = cell.balance_mask & 0x0fff
    while (mask !== 0) {
      activeCells += mask & 1
      mask >>>= 1
    }
  }
  if (activeCells === 0) return { label: 'Idle', state: 'idle' }
  return { label: `Active · ${activeCells} cell${activeCells === 1 ? '' : 's'}`, state: 'active' }
}
export const describeWatchdogBreadcrumb = (transfer: number | undefined, diagnostic?: number): string | undefined => {
  const details: string[] = []
  if (diagnostic !== undefined && (diagnostic >>> 28) === 0xC) {
    const monitoringActive = (diagnostic & (1 << 27)) !== 0
    const stalledSources = (diagnostic >>> 25) & 0x03
    const bccPhases = ['Uninitialized', 'Loop start', 'Device initialization', 'Register initialization', 'Diagnostics', 'Measurement start', 'Measurement read', 'Register requests', 'Fault detection', 'Balancing', 'Presence check', 'Snapshot publication', 'Energy update', 'Delay', 'Critical']
    const pccPhases = ['Uninitialized', 'Idle', 'Waiting for PCC state lock', 'Running']
    const source = !monitoringActive
      ? 'Monitoring had not started'
      : stalledSources === 1
        ? 'Main/PCC progress stopped'
        : stalledSources === 2
          ? 'BCC progress stopped'
          : stalledSources === 3
            ? 'Main/PCC and BCC progress stopped'
            : 'No worker-specific stall classified; scheduler, interrupt, or global stall possible'
    const bccPhase = (diagnostic >>> 20) & 0x1f
    const pccPhase = (diagnostic >>> 16) & 0x0f
    const pccSequence = (diagnostic >>> 8) & 0xff
    const bccSequence = diagnostic & 0xff
    details.push(`${source} · PCC ${pccPhases[pccPhase] ?? `phase ${pccPhase}`} (sequence ${pccSequence}) · BCC ${bccPhases[bccPhase] ?? `phase ${bccPhase}`} (sequence ${bccSequence})`)
  }
  if (transfer !== undefined && (transfer >>> 28) === 0xB) {
    const pending = (transfer & (1 << 27)) !== 0
    const sequence = (transfer >>> 19) & 0xff
    const cid = (transfer >>> 13) & 0x3f
    const address = (transfer >>> 6) & 0x7f
    const command = (transfer >>> 4) & 0x03
    const rxCount = transfer & 0x0f
    details.push(`${pending ? 'In-flight' : 'Completed'} BCC transfer · CID ${cid} · register 0x${address.toString(16).padStart(2, '0').toUpperCase()} · command ${command} · RX ${rxCount} · sequence ${sequence}`)
  }
  return details.length > 0 ? details.join(' · ') : undefined
}
export const bmsStatusSummary = (status: Status | null): string => {
  if (!status) return 'Waiting for BMS status over the selected transport.'
  if (status.measurements_fresh) return 'Fresh BMS measurements are available.'
  const faults = setBits(status.bms_active_errors, bmsFaultNames).join(', ')
  return `${bmsStateName(status.bms_state)}: measurements are not fresh${faults ? ` (${faults})` : ''}.`
}
