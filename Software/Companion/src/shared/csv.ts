import { cellVoltageV, currentA, icCelsius, ntcCelsius, socPercent } from './model'
import type { Snapshot } from '@/transports/Transport'

export function csvHeader(snapshot: Snapshot): string[] {
  const fields = ['timestamp_ms', 'pack_voltage_v', 'bat_plus_v', 'load_plus_v', 'pack_current_a', 'soc_valid', 'soc_percent', 'goodwe_tx_cycles', 'goodwe_snapshot_unavailable_cycles', 'goodwe_tx_queue_failures', 'goodwe_last_tx_failure_id', 'goodwe_last_tx_failure_ms', 'goodwe_reported_458_voltage_v', 'goodwe_reported_458_current_a', 'goodwe_fdcan_tx_error_count', 'goodwe_fdcan_rx_error_count', 'goodwe_fdcan_error_logging_count', 'goodwe_fdcan_last_error_code', 'goodwe_fdcan_activity', 'goodwe_fdcan_error_passive', 'goodwe_fdcan_warning', 'goodwe_fdcan_bus_off', 'goodwe_fdcan_hal_error_code', 'goodwe_fdcan_tx_fifo_free_level']
  for (const id of [0x453, 0x455, 0x456, 0x457, 0x458, 0x45a, 0x460]) fields.push(`goodwe_tx_${id.toString(16)}_success_count`, `goodwe_tx_${id.toString(16)}_last_success_ms`)
  for (const id of [0x420, 0x425, 0x305]) fields.push(`goodwe_rx_${id.toString(16)}_count`, `goodwe_rx_${id.toString(16)}_last_seen_ms`, `goodwe_rx_${id.toString(16)}_payload_hex`)
  fields.push('goodwe_rx_425_voltage_v_candidate', 'goodwe_rx_425_current_a_candidate')
  snapshot.cells.forEach(cell => cell.cell_voltage_uV.forEach((_, index) => fields.push(`slave_${cell.slave_index}_cell_${index}_v`)))
  snapshot.temperatures.forEach(temp => { temp.ntc_raw.forEach((_, index) => fields.push(`slave_${temp.slave_index}_ntc_${index}_c`)); fields.push(`slave_${temp.slave_index}_ic_c`) })
  return fields
}
export function csvRow(snapshot: Snapshot): string[] {
  const voltages = snapshot.hv_voltages
  const batteryVoltage = voltages?.valid ? cellVoltageV(voltages.bat_plus_uV).toFixed(6) : ''
  const loadVoltage = voltages?.valid ? cellVoltageV(voltages.load_plus_uV).toFixed(6) : ''
  const goodwe = snapshot.goodwe_can
  const txValues = [0x453, 0x455, 0x456, 0x457, 0x458, 0x45a, 0x460].flatMap(id => {
    const frame = goodwe?.transmit_frames.find(item => item.id === id)
    return frame ? [String(frame.success_count), String(frame.last_success_ms)] : ['', '']
  })
  const rxValues = [0x420, 0x425, 0x305].flatMap(id => {
    const frame = goodwe?.receive_frames.find(item => item.id === id)
    return frame ? [String(frame.count), String(frame.last_seen_ms), frame.data.map(value => value.toString(16).padStart(2, '0')).join('')] : ['', '', '']
  })
  const inverter425 = goodwe?.receive_frames.find(item => item.id === 0x425)
  const inverter425Voltage = inverter425 && inverter425.length >= 4 ? ((inverter425.data[0] | (inverter425.data[1] << 8)) / 10).toFixed(1) : ''
  const current425Raw = inverter425 && inverter425.length >= 4 ? inverter425.data[2] | (inverter425.data[3] << 8) : undefined
  const inverter425Current = current425Raw === undefined ? '' : ((current425Raw > 0x7fff ? current425Raw - 0x10000 : current425Raw) / 10).toFixed(1)
  const goodweValues = goodwe ? [String(goodwe.transmit_cycles), String(goodwe.snapshot_unavailable_cycles), String(goodwe.transmit_failures), goodwe.last_transmit_failure_id ? `0x${goodwe.last_transmit_failure_id.toString(16)}` : '', goodwe.last_transmit_failure_ms ? String(goodwe.last_transmit_failure_ms) : '', (goodwe.reported_458_voltage_deci_v / 10).toFixed(1), (goodwe.reported_458_current_deci_a / 10).toFixed(1), String(goodwe.transmit_error_count), String(goodwe.receive_error_count), String(goodwe.error_logging_count), String(goodwe.protocol_last_error_code), String(goodwe.protocol_activity), goodwe.error_passive ? 'true' : 'false', goodwe.warning ? 'true' : 'false', goodwe.bus_off ? 'true' : 'false', `0x${goodwe.hal_error_code.toString(16)}`, String(goodwe.transmit_fifo_free_level)] : Array(17).fill('')
  return [String(Date.now()), cellVoltageV(snapshot.pack.pack_voltage_uV).toFixed(6), batteryVoltage, loadVoltage, currentA(snapshot.pack.pack_current_raw).toFixed(4), snapshot.status.soc_valid ? 'true' : 'false', snapshot.status.soc_valid ? socPercent(snapshot.pack.soc_raw).toFixed(3) : '', ...goodweValues, ...txValues, ...rxValues, inverter425Voltage, inverter425Current, ...snapshot.cells.flatMap(cell => cell.cell_voltage_uV.map(value => cellVoltageV(value).toFixed(6))), ...snapshot.temperatures.flatMap(temp => [...temp.ntc_raw.map(value => ntcCelsius(value).toFixed(3)), icCelsius(temp.ic_temp_raw).toFixed(3)])]
}
export function downloadCsv(lines: string[]): void { const blob = new Blob([lines.join('\r\n')], { type: 'text/csv' }); const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = `flexbms-${new Date().toISOString().replace(/[:.]/g, '-')}.csv`; link.click(); URL.revokeObjectURL(link.href) }
