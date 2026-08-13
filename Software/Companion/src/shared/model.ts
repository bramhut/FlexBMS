import type { Snapshot } from '../transports/Transport'

export const cellVoltageV = (raw: number) => raw / 1_000_000
export const currentA = (raw: number) => raw / 64
export const socPercent = (raw: number) => 100 * (raw / 65535 * 3 - 1)
export const ntcCelsius = (raw: number) => raw / 65535 * 120 - 20
export const icCelsius = (raw: number) => raw / 10 - 273.15
export const isFresh = (snapshot: Snapshot | null) => snapshot !== null && snapshot.status.measurements_fresh
export const valueOrStale = (value: string, fresh: boolean) => fresh ? value : 'Stale'
export const bmsFaultNames = ['INVALID_CONFIG', 'TPL_FAULT', 'CID_INITIALIZATION_FAULT', 'REGISTER_INITIALIZATION_FAULT', 'CELL_BALANCING_FAULT', 'DIAGNOSTICS_FAULT', 'OVERVOLTAGE_LIMIT', 'UNDERVOLTAGE_LIMIT', 'TEMPERATURE_LIMIT', 'OVERCURRENT_LIMIT', 'IC_TEMPERATURE', 'SOC_LIMIT', 'OPEN_SHORT_FAULT', 'SYSTEM_FAULT', 'COMMUNICATION_TIMEOUT', 'HV_SUPERVISOR_FAULT']
export const hvReasonNames = ['SENSOR_DIAGNOSTIC', 'USB_ONLY', 'BATTERY_VOLTAGE_MISMATCH', 'LOAD_SIDE_ENERGIZED', 'PRECHARGE_TIMEOUT', 'PRECHARGE_VOLTAGE_LOST', 'CONTACTOR_VOLTAGE_LOST']
export const setBits = (mask: number, labels: string[]) => labels.filter((label, bit) => (mask & (1 << bit)) !== 0 ? label : false)
