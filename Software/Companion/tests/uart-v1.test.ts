import assert from 'node:assert/strict'
import test from 'node:test'
import { crc32, decodeHvVoltages, decodeStatus, encodeFrame, FrameDecoder, messageType, serviceId, writeLe32 } from '../src/shared/uartV1.ts'

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
