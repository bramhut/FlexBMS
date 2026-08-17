import assert from 'node:assert/strict'
import test from 'node:test'
import { bmsStatusSummary, currentA, icCelsius, isFresh, ntcCelsius, socPercent, valueOrStale } from '../src/shared/model.ts'
import { reconnectDelayMs } from '../src/shared/reconnect.ts'
import { serviceResultLabel } from '../src/shared/service.ts'
import { advancingUnixTime } from '../src/shared/time.ts'
import { unavailableCapabilities } from '../src/transports/Transport.ts'

test('raw UART v1 units convert in the presentation layer', () => {
  assert.equal(currentA(-64), -1)
  assert.equal(socPercent(65535), 200)
  assert.equal(ntcCelsius(0), -20)
  assert.equal(icCelsius(29430).toFixed(2), '21.15')
})
test('stale snapshots never render measurements as live zeroes', () => {
  assert.equal(isFresh(null), false)
  assert.equal(valueOrStale('0.000 V', false), 'Stale')
})
test('target capabilities disable unavailable functions', () => {
  const capabilities = unavailableCapabilities()
  assert.equal(capabilities.raw_terminal, false)
  assert.equal(capabilities.firmware_update, false)
  assert.equal(capabilities.wifi_configuration, false)
})
test('all named service results have UI labels', () => {
  assert.equal(serviceResultLabel('ok'), 'Accepted')
  assert.equal(serviceResultLabel('transport_error'), 'Transport error')
})
test('gateway reconnect is bounded exponential backoff', () => {
  assert.deepEqual([0, 1, 2, 3, 4, 5].map(reconnectDelayMs), [1000, 2000, 4000, 8000, 10000, 10000])
})
test('displayed RTC time advances from its device sample', () => {
  assert.equal(advancingUnixTime(1_786_655_390, 1_000, 4_999), 1_786_655_393)
})
test('stale BMS status identifies the state and active fault', () => {
  assert.equal(bmsStatusSummary({ bms_state: 3, hv_state: 0, flags: 0x10, slave_count: 1, bms_active_errors: 0x0002, bms_latched_errors: 0, hv_active_errors: 0, hv_latched_errors: 0, warnings: 0, uptime_ms: 0, measurements_fresh: false, run_request: false, balancing_request: false }), 'Error: measurements are not fresh (SLAVE_UNAVAILABLE).')
})
