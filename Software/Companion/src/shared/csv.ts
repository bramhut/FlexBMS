import { cellVoltageV, currentA, icCelsius, ntcCelsius, socPercent } from './model'
import type { Snapshot } from '@/transports/Transport'

export function csvHeader(snapshot: Snapshot): string[] {
  const fields = ['timestamp_ms', 'pack_voltage_v', 'pack_current_a', 'soc_percent']
  snapshot.cells.forEach(cell => cell.cell_voltage_uV.forEach((_, index) => fields.push(`slave_${cell.slave_index}_cell_${index}_v`)))
  snapshot.temperatures.forEach(temp => { temp.ntc_raw.forEach((_, index) => fields.push(`slave_${temp.slave_index}_ntc_${index}_c`)); fields.push(`slave_${temp.slave_index}_ic_c`) })
  return fields
}
export function csvRow(snapshot: Snapshot): string[] { return [String(Date.now()), cellVoltageV(snapshot.pack.pack_voltage_uV).toFixed(6), currentA(snapshot.pack.pack_current_raw).toFixed(4), socPercent(snapshot.pack.soc_raw).toFixed(3), ...snapshot.cells.flatMap(cell => cell.cell_voltage_uV.map(value => cellVoltageV(value).toFixed(6))), ...snapshot.temperatures.flatMap(temp => [...temp.ntc_raw.map(value => ntcCelsius(value).toFixed(3)), icCelsius(temp.ic_temp_raw).toFixed(3)])] }
export function downloadCsv(lines: string[]): void { const blob = new Blob([lines.join('\r\n')], { type: 'text/csv' }); const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = `flexbms-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`; link.click(); URL.revokeObjectURL(link.href) }
