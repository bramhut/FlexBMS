import assert from 'node:assert/strict'
import test from 'node:test'
import { crc32, decodeStatus, encodeFrame, FrameDecoder, messageType, serviceId } from '../src/shared/uartV1.ts'

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

test('UART v1 status exposes run, balancing, freshness, and SOC validity', () => {
  const payload = new Uint8Array(29)
  payload.set([3, 4, 0x7c, 0x00, 1, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 1, 0, 0, 0])
  const status = decodeStatus(payload)
  assert.equal(status?.run_request, true)
  assert.equal(status?.balancing_request, true)
  assert.equal(status?.measurements_fresh, true)
  assert.equal(status?.soc_valid, true)
  assert.equal(status?.bms_latched_errors, 8)
  assert.equal(status?.warnings, 1)
})
