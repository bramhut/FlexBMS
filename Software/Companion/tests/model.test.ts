import assert from 'node:assert/strict'
import test from 'node:test'
import { balancingDisplay, bmsStatusSummary, currentA, describeWatchdogBreadcrumb, formatEnergy, formatPower, icCelsius, isFresh, ntcCelsius, powerW, socPercent, valueOrStale, warningDisplayNames } from '../src/shared/model.ts'
import { reconnectDelayMs } from '../src/shared/reconnect.ts'
import { serviceResultLabel } from '../src/shared/service.ts'
import { advancingUnixTime, advancingUptimeMs, createUptimeTracker, displayedUptimeMs, formatUptime, sampleUptime } from '../src/shared/time.ts'
import { unavailableCapabilities } from '../src/transports/Transport.ts'

test('raw UART v1 units convert in the presentation layer', () => {
  assert.equal(currentA(-64), -1)
  assert.equal(socPercent(65535), 200)
  assert.equal(ntcCelsius(0), -20)
  assert.equal(icCelsius(29430).toFixed(2), '21.15')
})
test('energy display selects a useful unit for the current quantity', () => {
  assert.equal(formatEnergy('12'), '12 µWh')
  assert.equal(formatEnergy('12345'), '12.35 mWh')
  assert.equal(formatEnergy('12345678'), '12.35 Wh')
  assert.equal(formatEnergy('1234567890'), '1.235 kWh')
  assert.equal(formatEnergy('123', false), 'Unavailable')
})
test('power display selects W or four-significant-digit kW', () => {
  assert.equal(powerW(320_820_000, 2284), 320.82 * (2284 / 64))
  assert.equal(formatPower(999.6), '1000 W')
  assert.equal(formatPower(1_234.56), '1.235 kW')
  assert.equal(formatPower(12_345.6), '12.35 kW')
  assert.equal(formatPower(123_456), '123.5 kW')
  assert.equal(formatPower(1234, false), 'Unavailable')
})
test('stale snapshots never render measurements as live zeroes', () => {
  assert.equal(isFresh(null), false)
  assert.equal(valueOrStale('0.000 V', false), 'Stale')
})
test('OFF-state voltage mismatch has an operator-facing warning label', () => {
  assert.equal(warningDisplayNames[2], 'Pack-voltage mismatch (HV off)')
})
test('transient BCC communication has an operator-facing warning label', () => {
  assert.equal(warningDisplayNames[3], 'BCC communication retry')
})
test('watchdog diagnostics identify the stalled source and active phases', () => {
  const transfer = 0xB4A10052
  const diagnostic = 0xCA921294
  const description = describeWatchdogBreadcrumb(transfer, diagnostic)
  assert.match(description ?? '', /Main\/PCC progress stopped/)
  assert.match(description ?? '', /PCC Waiting for PCC state lock/)
  assert.match(description ?? '', /BCC Balancing/)
  assert.match(description ?? '', /Completed BCC transfer/)
})
test('balancing summary distinguishes disabled, idle, active, and fault states', () => {
  const status = { bms_state: 2, hv_state: 4, flags: 0, slave_count: 2, bms_active_errors: 0, bms_latched_errors: 0, hv_active_errors: 0, hv_latched_errors: 0, warnings: 0, uptime_ms: 0, measurements_fresh: true, run_request: true, balancing_enabled: true, soc_valid: true, current_sensing_enabled: true }
  const cells = [
    { slave_index: 0, balance_mask: 0x0005, cell_voltage_uV: Array(12).fill(3_400_000) },
    { slave_index: 1, balance_mask: 0x0802, cell_voltage_uV: Array(12).fill(3_400_000) },
  ]
  assert.deepEqual(balancingDisplay({ ...status, balancing_enabled: false }, cells), { label: 'Disabled', state: 'disabled' })
  assert.deepEqual(balancingDisplay(status, cells.map(cell => ({ ...cell, balance_mask: 0 }))), { label: 'Idle', state: 'idle' })
  assert.deepEqual(balancingDisplay(status, cells), { label: 'Active · 4 cells', state: 'active' })
  assert.deepEqual(balancingDisplay({ ...status, bms_latched_errors: 1 << 8 }, cells), { label: 'Fault', state: 'fault' })
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
test('displayed controller uptime advances locally and stays compact', () => {
  assert.equal(advancingUptimeMs(10_000, 1_000, 4_999), 13_999)
  assert.equal(advancingUptimeMs(10_000, 5_000, 1_000), 10_000)
  assert.equal(formatUptime(97_322_000), '1 d 3 h 2 min')
})
test('uptime display stays monotonic when samples arrive with jitter', () => {
  const tracker = createUptimeTracker()
  assert.equal(sampleUptime(tracker, 10_000, 1_000), 'accepted')
  assert.equal(displayedUptimeMs(tracker, 2_000), 11_000)
  assert.equal(sampleUptime(tracker, 10_500, 2_000), 'accepted')
  assert.equal(displayedUptimeMs(tracker, 2_000), 11_000)
  assert.equal(displayedUptimeMs(tracker, 2_500), 11_000)
  assert.equal(displayedUptimeMs(tracker, 3_000), 11_500)
})
test('uptime tracker ignores delayed samples and recognizes reset boundaries', () => {
  const tracker = createUptimeTracker()
  assert.equal(sampleUptime(tracker, 2_000_000, 1_000), 'accepted')
  assert.equal(sampleUptime(tracker, 1_990_000, 2_000), 'stale')
  assert.equal(displayedUptimeMs(tracker, 3_000), 2_002_000)
  assert.equal(sampleUptime(tracker, 500, 4_000), 'restart')
  assert.equal(displayedUptimeMs(tracker, 4_000), 500)
  const wrapTracker = createUptimeTracker()
  assert.equal(sampleUptime(wrapTracker, 0x0000_0100, 5_000), 'accepted')
  assert.equal(sampleUptime(wrapTracker, 0xf100_0000, 6_000), 'accepted')
  assert.equal(sampleUptime(wrapTracker, 0x0000_0200, 7_000), 'wrap')
})
test('stale BMS status identifies the state and active fault', () => {
  assert.equal(bmsStatusSummary({ bms_state: 3, hv_state: 0, flags: 0x10, slave_count: 1, bms_active_errors: 0x0002, bms_latched_errors: 0, hv_active_errors: 0, hv_latched_errors: 0, warnings: 0, uptime_ms: 0, measurements_fresh: false, run_request: false, balancing_enabled: false, soc_valid: false, current_sensing_enabled: false }), 'Error: measurements are not fresh (SLAVE_UNAVAILABLE).')
})
