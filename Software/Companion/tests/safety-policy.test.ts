import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { decodeSafetyPolicy, safetyFields } from '../src/shared/safetyPolicy.ts'
const bytes = JSON.parse(readFileSync(new URL('./fixtures/safety-policy.json', import.meta.url), 'utf8'))
test('safety policy decodes scaled, signed values and rejects unknown/truncated schemas', () => {
  const p = decodeSafetyPolicy(bytes)!
  assert.equal(safetyFields.length, 43)
  assert.equal(p.capacity_mah, 314000)
  assert.equal(p.bal_min_ma, -100)
  assert.equal(p.cell_ov_programmed_uv, 3588000)
  assert.equal(p.cold_allowance_deci_c, 30)
  assert.equal(p.hot_stop_deci_c, 550)
  assert.equal(decodeSafetyPolicy([2, ...bytes.slice(1)]), null)
  assert.equal(decodeSafetyPolicy(bytes.slice(0, -1)), null)
  assert.equal(decodeSafetyPolicy([...bytes, 0]), null)
  assert.equal(decodeSafetyPolicy([1, 42, ...bytes.slice(2)]), null)
  assert.equal(decodeSafetyPolicy([1, 43, -1, ...bytes.slice(3)]), null)
  assert.equal(decodeSafetyPolicy(undefined), null)
})
test('safety policy field order matches the STM32 encoder contract', () => {
  const header = readFileSync(new URL('../../Master/Inc/Peripherals/SafetyPolicy.h', import.meta.url), 'utf8')
  const fields = header.split('enum Field : size_t {')[1].split('Count')[0].split(',').map(s => s.trim()).filter(Boolean)
  assert.deepEqual(fields, [...safetyFields])
})
