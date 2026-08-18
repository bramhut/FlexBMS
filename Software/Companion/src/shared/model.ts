import type { Snapshot, Status } from '../transports/Transport'

export const cellVoltageV = (raw: number) => raw / 1_000_000
export const currentA = (raw: number) => raw / 64
export const socPercent = (raw: number) => 100 * (raw / 65535 * 3 - 1)
export const ntcCelsius = (raw: number) => raw / 65535 * 120 - 20
// The STM32 BCC driver exports IC temperature as centikelvin.
export const icCelsius = (raw: number) => raw / 100 - 273.15
export const isFresh = (snapshot: Snapshot | null) => snapshot !== null && snapshot.status.measurements_fresh
export const valueOrStale = (value: string, fresh: boolean) => fresh ? value : 'Stale'
export const bmsStateName = (value: number) => ['Starting', 'Ready', 'Running', 'Error', 'Critical'][value] ?? `Unknown (${value})`
export const hvStateName = (value: number) => ['Off', 'Self-test', 'Precharge', 'Contactor close', 'Run'][value] ?? `Unknown (${value})`
export const bmsFaultNames = ['CONFIGURATION_INVALID', 'SLAVE_UNAVAILABLE', 'BCC_DIAGNOSTICS', 'CELL_VOLTAGE_LIMIT', 'THERMAL_LIMIT', 'CURRENT_LIMIT', 'BCC_INTEGRITY', 'ADC_FAULT', 'BALANCING_HARDWARE_FAULT', 'BCC_COMMUNICATION']
export const hvReasonNames = ['HV_SENSOR_DIAGNOSTIC', 'BATTERY_VOLTAGE_MISMATCH', 'LOAD_SIDE_ENERGISED', 'PRECHARGE_TIMEOUT', 'PRECHARGE_VOLTAGE_LOST', 'CONTACTOR_VOLTAGE_LOST']
export const warningNames = ['WATCHDOG_RESET', 'STARTUP_DIAGNOSTICS_BYPASSED', 'BATTERY_VOLTAGE_MISMATCH_OFF']
export const setBits = (mask: number, labels: string[]) => labels.filter((label, bit) => (mask & (1 << bit)) !== 0 ? label : false)
export const bmsStatusSummary = (status: Status | null): string => {
  if (!status) return 'Waiting for BMS status over the selected transport.'
  if (status.measurements_fresh) return 'Fresh BMS measurements are available.'
  const faults = setBits(status.bms_active_errors, bmsFaultNames).join(', ')
  return `${bmsStateName(status.bms_state)}: measurements are not fresh${faults ? ` (${faults})` : ''}.`
}
