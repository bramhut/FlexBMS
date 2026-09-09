import assert from 'node:assert/strict'
import test from 'node:test'
import { crc32, decodeEnergy, decodeGoodweCanDiagnostics, decodeHvVoltages, decodeStatus, encodeFrame, FrameDecoder, messageType, serviceId, writeLe16, writeLe32 } from '../src/shared/uartV1.ts'

test('UART v1 browser codec matches the canonical heartbeat vector', () => {
  const heartbeat = encodeFrame({ type: messageType.heartbeat, sequence: 0, payload: new Uint8Array() })
  assert.deepEqual([...heartbeat], [0x46, 0x42, 0x01, 0x01, 0x00, 0x00, 0x00, 0x8f, 0x7a, 0xfb, 0x7d])
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926)
})

test('UART v1 decoder handles fragmented frames and resynchronises after bad CRC', () => {
  const request = encodeFrame({ type: messageType.serviceRequest, sequence: 42, payload: Uint8Array.of(serviceId.getStatus) })
  const decoder = new FrameDecoder()
  assert.deepEqual(decoder.consume(request.slice(0, 4)), [])
  assert.deepEqual(decoder.consume(request.slice(4, 9)), [])
  assert.deepEqual(decoder.consume(request.slice(9)), [{ type: messageType.serviceRequest, sequence: 42, payload: Uint8Array.of(serviceId.getStatus) }])
  const corrupt = request.slice(); corrupt[corrupt.length - 1] ^= 0xff
  assert.deepEqual(decoder.consume(corrupt), [])
  assert.deepEqual(decoder.consume(request), [{ type: messageType.serviceRequest, sequence: 42, payload: Uint8Array.of(serviceId.getStatus) }])
})

test('UART v1 decodes valid AMC3330 BAT+ and LOAD+ telemetry', () => {
  const payload = new Uint8Array(12)
  payload[0] = 1
  writeLe32(payload, 4, 301_234_567)
  writeLe32(payload, 8, 12_345)
  assert.deepEqual(decodeHvVoltages(payload), { valid: true, bat_plus_uV: 301_234_567, load_plus_uV: 12_345 })
  assert.equal(decodeHvVoltages(payload.slice(0, 11)), undefined)
})

test('UART v1 decodes 64-bit energy counters and rejects malformed payloads', () => {
  const payload = Uint8Array.from([1, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x99, 0x00, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff])
  assert.deepEqual(decodeEnergy(payload), { valid: true, charged_energy_uWh: '1234605616436508552', discharged_energy_uWh: '18441921395520307353' })
  assert.equal(decodeEnergy(payload.slice(0, 16)), undefined)
})

test('UART v1 decodes GoodWe CAN counters, timestamps, signed current, and raw replies', () => {
  const payload = new Uint8Array(156)
  payload[0] = 1
  payload[1] = 1
  payload[2] = 3
  writeLe32(payload, 4, 123)
  writeLe32(payload, 12, 2)
  writeLe32(payload, 16, 42_000)
  writeLe16(payload, 20, 0x458)
  writeLe16(payload, 22, 0xffd3)
  writeLe16(payload, 24, 3290)
  payload[26] = 3
  payload[27] = 4
  payload[28] = 5
  payload[29] = 6
  payload[30] = 8
  payload[31] = 4
  writeLe32(payload, 32, 0x200)
  writeLe32(payload, 36, 2)
  writeLe32(payload, 40, 120)
  writeLe32(payload, 44, 41_500)
  writeLe32(payload, 116, 9)
  writeLe32(payload, 120, 41_900)
  payload[124] = 4
  payload.set([0xda, 0x0c, 0x25, 0x00], 125)
  const diagnostics = decodeGoodweCanDiagnostics(payload)
  assert.equal(diagnostics?.transmit_cycles, 123)
  assert.equal(diagnostics?.transmit_failures, 2)
  assert.equal(diagnostics?.last_transmit_failure_id, 0x458)
  assert.equal(diagnostics?.reported_458_current_deci_a, -45)
  assert.equal(diagnostics?.reported_458_voltage_deci_v, 3290)
  assert.equal(diagnostics?.transmit_error_count, 3)
  assert.equal(diagnostics?.receive_error_count, 4)
  assert.equal(diagnostics?.bus_off, true)
  assert.equal(diagnostics?.hal_error_code, 0x200)
  assert.equal(diagnostics?.transmit_fifo_free_level, 2)
  assert.deepEqual(diagnostics?.transmit_frames[0], { id: 0x453, success_count: 120, last_success_ms: 41_500 })
  assert.deepEqual(diagnostics?.receive_frames[1], { id: 0x425, count: 9, last_seen_ms: 41_900, length: 4, data: [0xda, 0x0c, 0x25, 0x00] })
  payload[0] = 2
  assert.equal(decodeGoodweCanDiagnostics(payload), undefined)
})

test('UART v1 status exposes current sensing and SOC calibration validity', () => {
  const payload = new Uint8Array(33)
  payload.set([3, 4, 0xfc, 0x01, 1, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 1, 0, 0, 0, 0x80, 0x51, 0x37, 0x66])
  const status = decodeStatus(payload)
  assert.equal(status?.run_request, true)
  assert.equal(status?.balancing_enabled, true)
  assert.equal(status?.measurements_fresh, true)
  assert.equal(status?.soc_valid, true)
  assert.equal(status?.current_sensing_enabled, true)
  assert.equal(status?.soc_last_calibration_unix_s, 1714901376)
  assert.equal(status?.bms_latched_errors, 8)
  assert.equal(status?.warnings, 1)
})

test('UART v1 status omits the SOC calibration time without its validity flag', () => {
  const payload = new Uint8Array(33)
  payload.set([3, 0, 0x80, 0x00, 1])
  payload.set([0xff, 0xff, 0xff, 0xff], 29)
  const status = decodeStatus(payload)
  assert.equal(status?.current_sensing_enabled, true)
  assert.equal(status?.soc_last_calibration_unix_s, undefined)
})

test('UART v1 status decodes transfer and watchdog diagnostic breadcrumbs', () => {
  const payload = new Uint8Array(41)
  payload.set([2, 4, 0x00, 0x06, 8])
  writeLe32(payload, 33, 0xB4A10052)
  writeLe32(payload, 37, 0xCA921294)
  const status = decodeStatus(payload)
  assert.equal(status?.watchdog_breadcrumb, 0xB4A10052)
  assert.equal(status?.watchdog_diagnostic, 0xCA921294)
})
