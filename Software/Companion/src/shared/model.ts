import type { Snapshot, Status } from '../transports/Transport'

export const cellVoltageV = (raw: number) => raw / 1_000_000
export const currentA = (raw: number) => raw / 64
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
export const isFresh = (snapshot: Snapshot | null) => snapshot !== null && snapshot.status.measurements_fresh
export const valueOrStale = (value: string, fresh: boolean) => fresh ? value : 'Stale'
export const bmsStateName = (value: number) => ['Starting', 'Ready', 'Running', 'Error', 'Critical'][value] ?? `Unknown (${value})`
export const hvStateName = (value: number) => ['Off', 'Self-test', 'Precharge', 'Contactor close', 'Run'][value] ?? `Unknown (${value})`
export const bmsFaultNames = ['CONFIGURATION_INVALID', 'SLAVE_UNAVAILABLE', 'BCC_DIAGNOSTICS', 'CELL_VOLTAGE_LIMIT', 'THERMAL_LIMIT', 'CURRENT_LIMIT', 'BCC_INTEGRITY', 'ADC_FAULT', 'BALANCING_HARDWARE_FAULT', 'BCC_COMMUNICATION', 'NO_CONFIG']
export const bccDiagnosticNames = ['ADC1 channel verification', 'OV/UV functional verification', 'OV/UV detection', 'Cell-terminal open/short detection', 'Cell-voltage channel verification', 'Cell contact resistance', 'Cell-terminal leakage', 'Current measurement', 'Shunt connection', 'GPIO over/under-temperature', 'GPIO open-terminal detection', 'Cell-balancing open-load detection']
export const bccDiagnosticStatusNames = ['Success', 'Parameter out of range', 'SPI communication failure', 'Communication timeout', 'Communication echo mismatch', 'Communication CRC error', 'Communication message-counter error', 'Empty communication response', 'Cannot enter diagnostic mode', 'Conversion data not ready']
export const hvReasonNames = ['HV_SENSOR_DIAGNOSTIC', 'BATTERY_VOLTAGE_MISMATCH', 'LOAD_SIDE_ENERGISED', 'PRECHARGE_TIMEOUT', 'PRECHARGE_VOLTAGE_LOST', 'CONTACTOR_VOLTAGE_LOST']
export const warningNames = ['WATCHDOG_RESET', 'STARTUP_DIAGNOSTICS_BYPASSED', 'BATTERY_VOLTAGE_MISMATCH_OFF']
export const warningDisplayNames = ['Watchdog reset', 'Startup diagnostics bypassed', 'Pack-voltage mismatch (HV off)']
export const setBits = (mask: number, labels: string[]) => labels.filter((label, bit) => (mask & (1 << bit)) !== 0 ? label : false)
export const bmsStatusSummary = (status: Status | null): string => {
  if (!status) return 'Waiting for BMS status over the selected transport.'
  if (status.measurements_fresh) return 'Fresh BMS measurements are available.'
  const faults = setBits(status.bms_active_errors, bmsFaultNames).join(', ')
  return `${bmsStateName(status.bms_state)}: measurements are not fresh${faults ? ` (${faults})` : ''}.`
}
