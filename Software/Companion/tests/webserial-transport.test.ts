import { readFileSync } from 'node:fs'
import { decodeSafetyPolicy } from '../src/shared/safetyPolicy.ts'
import assert from 'node:assert/strict'
import test from 'node:test'
import { WebSerialTransport } from '../src/transports/WebSerialTransport.ts'
import { encodeFrame, FrameDecoder, messageType, serviceId, writeLe16, writeLe32 } from '../src/shared/uartV1.ts'

type TestPort = {
  port: { open(options: { baudRate: number }): Promise<void>; close(): Promise<void>; readonly readable: ReadableStream<Uint8Array>; writable: WritableStream<Uint8Array> }
  readonly reader: ReadableStreamDefaultController<Uint8Array>
  writes: Uint8Array[]
}

function makePort(): TestPort {
  let reader!: ReadableStreamDefaultController<Uint8Array>
  let readable: ReadableStream<Uint8Array>
  let openCount = 0
  const makeReadable = () => new ReadableStream<Uint8Array>({ start(controller) { reader = controller } })
  readable = makeReadable()
  const writes: Uint8Array[] = []
  return {
    port: {
      async open(): Promise<void> { if (openCount++ > 0) readable = makeReadable() },
      async close(): Promise<void> { reader.close() },
      get readable() { return readable },
      writable: new WritableStream<Uint8Array>({ write(frame) { writes.push(frame.slice()) } }),
    },
    get reader() { return reader },
    writes,
  }
}

async function withSerial<T>(run: (fixture: TestPort) => Promise<T>): Promise<T> {
  const fixture = makePort()
  const originalNavigator = Object.getOwnPropertyDescriptor(globalThis, 'navigator')
  const originalWindow = Object.getOwnPropertyDescriptor(globalThis, 'window')
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { serial: { requestPort: async () => fixture.port } } })
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { setInterval, clearInterval, setTimeout, clearTimeout } })
  try { return await run(fixture) } finally {
    if (originalNavigator) Object.defineProperty(globalThis, 'navigator', originalNavigator)
    else delete (globalThis as { navigator?: Navigator }).navigator
    if (originalWindow) Object.defineProperty(globalThis, 'window', originalWindow)
    else delete (globalThis as { window?: Window }).window
  }
}

test('direct USB correlates a BUSY service response by frame sequence', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const response = transport.request('acknowledge_faults', {})
    await new Promise(resolve => setTimeout(resolve, 0))
    const decoder = new FrameDecoder()
    const requests = writes.flatMap(frame => decoder.consume(frame)).filter(frame => frame.type === messageType.serviceRequest)
    assert.equal(requests.length, 1)
    assert.equal(requests[0].sequence, 1)
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: 1, payload: Uint8Array.of(serviceId.acknowledgeFaults, 3) }))
    assert.equal((await response).result, 'busy')
    transport.disconnect()
  })
})

test('direct USB resolves pending services when the port disconnects', async () => {
  await withSerial(async () => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const response = transport.request('acknowledge_faults', {})
    transport.disconnect()
    assert.equal((await response).result, 'transport_error')
  })
})

test('direct USB encodes and decodes runtime configuration', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const responsePromise = transport.request('get_config', {})
    await new Promise(resolve => setTimeout(resolve, 0))
    const decoder = new FrameDecoder()
    const request = writes.flatMap(frame => decoder.consume(frame)).find(frame => frame.type === messageType.serviceRequest)
    assert.ok(request)
    assert.deepEqual(Array.from(request!.payload), [serviceId.getConfig])
    const data = new Uint8Array(19)
    data[0] = 0
    writeLe16(data, 1, 4)
    writeLe16(data, 3, 4)
    data[5] = 1
    data[6] = 1
    writeLe32(data, 7, 10000)
    writeLe32(data, 11, 314000)
    data[16] = 1
    data[17] = 1
    data[18] = 10
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request!.sequence, payload: Uint8Array.of(serviceId.getConfig, 0, ...data) }))
    const response = await responsePromise
    assert.equal(response.result, 'ok')
    assert.deepEqual(response.data, { reason: 'valid', expected_version: 4, stored_version: 4, slave_count: 1, current_sense_slave: 1, shunt_resistance_uohm: 10000, battery_capacity_mah: 314000, invert_current: false, balance_enabled: true, startup_diagnostics: true, self_discharge_tenth_percent_per_30_days: 10, cold_allowance_deci_c: 30 })
    transport.disconnect()
  })
})

test('direct USB reads configuration v5 including both cold-allowance endpoints', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    try {
      for (const allowance of [0, 100]) {
        const pending = transport.request('get_config', {})
        await new Promise(resolve => setTimeout(resolve, 0))
        const decoder = new FrameDecoder()
        const request = writes.flatMap(frame => decoder.consume(frame)).filter(frame => frame.type === messageType.serviceRequest).at(-1)!
        const data = new Uint8Array(20)
        writeLe16(data, 1, 5)
        writeLe16(data, 3, 5)
        data[5] = 8
        data[6] = 1
        writeLe32(data, 7, 375)
        writeLe32(data, 11, 314000)
        data[16] = data[17] = 1
        data[18] = 10
        data[19] = allowance
        reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request.sequence, payload: Uint8Array.of(serviceId.getConfig, 0, ...data) }))
        const response = await pending
        assert.equal(response.result, 'ok')
        assert.equal(response.data?.cold_allowance_deci_c, allowance)
        assert.equal(response.data?.slave_count, 8)
        assert.equal(response.data?.self_discharge_tenth_percent_per_30_days, 10)
      }
    } finally { transport.disconnect() }
  })
})

test('direct USB encodes balance-enabled runtime configuration', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    try {
      await transport.connect()
      const responsePromise = transport.request('set_config', { slave_count: 1, current_sense_slave: 1, shunt_resistance_uohm: 10000, battery_capacity_mah: 314000, invert_current: false, balance_enabled: false, startup_diagnostics: true, self_discharge_tenth_percent_per_30_days: 10, cold_allowance_deci_c: 30 })
      await new Promise(resolve => setTimeout(resolve, 0))
      const decoder = new FrameDecoder()
      const request = writes.flatMap(frame => decoder.consume(frame)).find(frame => frame.type === messageType.serviceRequest)
      assert.ok(request)
      assert.deepEqual(Array.from(request!.payload), [serviceId.setConfig, 1, 1, 0x10, 0x27, 0, 0, 0x90, 0xca, 4, 0, 0, 0, 1, 10, 30])
      reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request!.sequence, payload: Uint8Array.of(serviceId.setConfig, 0) }))
      assert.equal((await responsePromise).result, 'ok')
    } finally {
      transport.disconnect()
    }
  })
})

test('direct USB decodes a per-slave BCC diagnostic report', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const responsePromise = transport.request('get_diagnostic_report', { slave_index: 0 })
    await new Promise(resolve => setTimeout(resolve, 0))
    const decoder = new FrameDecoder()
    const request = writes.flatMap(frame => decoder.consume(frame)).find(frame => frame.type === messageType.serviceRequest)
    assert.ok(request)
    assert.deepEqual(Array.from(request!.payload), [serviceId.getDiagnosticReport, 0])
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request!.sequence, payload: Uint8Array.of(serviceId.getDiagnosticReport, 0, 0, 1, 0x10, 0, 3, 2) }))
    assert.deepEqual((await responsePromise).data, { slave_index: 0, cid: 1, failed_checks: 0x10, status_code: 3, failed_diagnostic: 2 })
    transport.disconnect()
  })
})

test('direct USB includes AMC3330 BAT+ and LOAD+ telemetry in a complete snapshot', async () => {
  await withSerial(async ({ reader }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const snapshot = new Promise<Parameters<Parameters<typeof transport.onSnapshot>[0]>[0]>(resolve => transport.onSnapshot(resolve))
    const status = new Uint8Array(33)
    writeLe16(status, 2, 1 << 3)
    status[4] = 1
    const hvVoltages = new Uint8Array(12)
    hvVoltages[0] = 1
    writeLe32(hvVoltages, 4, 302_500_000)
    writeLe32(hvVoltages, 8, 1_250_000)
    const calibration = new Uint8Array(16)
    calibration[0] = 1
    calibration[1] = 3
    writeLe16(calibration, 2, 44083)
    writeLe32(calibration, 4, 1_714_901_376)
    writeLe32(calibration, 12, 300_000)
    reader.enqueue(encodeFrame({ type: messageType.status, sequence: 0, payload: status }))
    reader.enqueue(encodeFrame({ type: messageType.socCalibration, sequence: 0, payload: calibration }))
    reader.enqueue(encodeFrame({ type: messageType.hvVoltages, sequence: 0, payload: hvVoltages }))
    reader.enqueue(encodeFrame({ type: messageType.pack, sequence: 0, payload: Uint8Array.from([2, ...new Array(26).fill(0)]) }))
    reader.enqueue(encodeFrame({ type: messageType.cell, sequence: 0, payload: new Uint8Array(51) }))
    reader.enqueue(encodeFrame({ type: messageType.temperature, sequence: 0, payload: new Uint8Array(11) }))
    const received = await snapshot
    assert.deepEqual(received.hv_voltages, { valid: true, bat_plus_uV: 302_500_000, load_plus_uV: 1_250_000 })
    assert.deepEqual(received.status.soc_calibration, { valid: true, pre_soc_raw: 44083, unix_time_s: 1_714_901_376, qualifying_dwell_ms: 300_000 })
    transport.disconnect()
  })
})

test('direct USB caches balancing totals per slave, replaces totals, and clears them on restart and reconnect', async () => {
  await withSerial(async fixture => {
    const transport = new WebSerialTransport()
    const snapshots: Array<Parameters<Parameters<typeof transport.onSnapshot>[0]>[0]> = []
    transport.onSnapshot(snapshot => snapshots.push(snapshot))
    await transport.connect()

    const send = (...frames: Array<{ type: number; payload: Uint8Array }>) => {
      for (const frame of frames) fixture.reader.enqueue(encodeFrame({ ...frame, sequence: 0 }))
    }
    const makeStatus = (uptimeMs: number) => {
      const payload = new Uint8Array(33)
      writeLe16(payload, 2, 1 << 3)
      payload[4] = 2
      writeLe32(payload, 25, uptimeMs)
      return payload
    }
    const makeCell = (slaveIndex: number) => {
      const payload = new Uint8Array(51)
      payload[0] = slaveIndex
      return payload
    }
    const makeTemperature = (slaveIndex: number) => {
      const payload = new Uint8Array(11)
      payload[0] = slaveIndex
      return payload
    }
    const makeCharge = (slaveIndex: number, values: number[]) => {
      const payload = new Uint8Array(2 + values.length * 4)
      payload[0] = slaveIndex
      payload[1] = values.length
      values.forEach((value, index) => writeLe32(payload, 2 + index * 4, value))
      return payload
    }
    const flush = async () => { await new Promise(resolve => setTimeout(resolve, 0)) }
    const sendMeasurements = (uptimeMs: number) => send(
      { type: messageType.status, payload: makeStatus(uptimeMs) },
      { type: messageType.pack, payload: Uint8Array.from([2, ...new Array(26).fill(0)]) },
      { type: messageType.cell, payload: makeCell(0) },
      { type: messageType.temperature, payload: makeTemperature(0) },
      { type: messageType.cell, payload: makeCell(1) },
      { type: messageType.temperature, payload: makeTemperature(1) },
    )
    const waitForSnapshot = async (minimumCount: number) => {
      for (let attempt = 0; attempt < 20 && snapshots.length < minimumCount; attempt += 1) await flush()
      assert.ok(snapshots.length >= minimumCount, `expected at least ${minimumCount} snapshots; got ${snapshots.length}`)
      return snapshots.at(-1)!
    }
    const cellTotals = (snapshot: typeof snapshots[number]) =>
      Object.fromEntries(snapshot.cells.map(cell => [cell.slave_index, cell.balancing_mAh]))

    // Legacy/optional diagnostics must not gate a complete ordinary snapshot.
    sendMeasurements(100_000)
    let snapshot = await waitForSnapshot(1)
    assert.deepEqual(cellTotals(snapshot), { 0: undefined, 1: undefined })

    send({ type: messageType.balancingCharge, payload: makeCharge(1, [101, 202]) })
    snapshot = await waitForSnapshot(2)
    assert.deepEqual(cellTotals(snapshot), { 0: undefined, 1: [101, 202] })

    send({ type: messageType.balancingCharge, payload: makeCharge(0, [11, 22]) })
    snapshot = await waitForSnapshot(3)
    assert.deepEqual(cellTotals(snapshot), { 0: [11, 22], 1: [101, 202] })

    // Frames carry cumulative snapshots; a newer frame replaces the prior total.
    send({ type: messageType.balancingCharge, payload: makeCharge(1, [303, 404]) })
    snapshot = await waitForSnapshot(4)
    assert.deepEqual(cellTotals(snapshot), { 0: [11, 22], 1: [303, 404] })

    // A decreasing controller uptime identifies an STM32 restart and clears cached totals.
    send({ type: messageType.status, payload: makeStatus(25) })
    snapshot = await waitForSnapshot(5)
    assert.deepEqual(cellTotals(snapshot), { 0: undefined, 1: undefined })

    send(
      { type: messageType.balancingCharge, payload: makeCharge(0, [9, 10]) },
      { type: messageType.balancingCharge, payload: makeCharge(1, [8, 7]) },
    )
    snapshot = await waitForSnapshot(7)
    assert.deepEqual(cellTotals(snapshot), { 0: [9, 10], 1: [8, 7] })

    transport.disconnect()
    await flush()
    await transport.connect()
    sendMeasurements(10)
    snapshot = await waitForSnapshot(8)
    assert.deepEqual(cellTotals(snapshot), { 0: undefined, 1: undefined })
    transport.disconnect()
  })
})

test('direct USB safety request preserves the full versioned policy without truncation', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    try {
      const pending = transport.request('get_safety_limits', {})
      await new Promise(resolve => setTimeout(resolve, 0))
      const decoder = new FrameDecoder()
      const request = writes.flatMap(frame => decoder.consume(frame)).find(frame => frame.type === messageType.serviceRequest)!
      assert.deepEqual([...request.payload], [0x0e])
      const bytes = JSON.parse(readFileSync(new URL('./fixtures/safety-policy.json', import.meta.url), 'utf8'))
      reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request.sequence, payload: Uint8Array.of(0x0e, 0, ...bytes) }))
      const result = await pending
      assert.equal(result.result, 'ok')
      assert.deepEqual(result.data?.safety_limits_payload, bytes)
      assert.equal(decodeSafetyPolicy(result.data?.safety_limits_payload)?.bal_min_ma, -100)
    } finally { transport.disconnect() }
  })
})
