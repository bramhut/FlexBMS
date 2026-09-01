import assert from 'node:assert/strict'
import test from 'node:test'
import { WebSerialTransport } from '../src/transports/WebSerialTransport.ts'
import { encodeFrame, FrameDecoder, messageType, serviceId, writeLe16, writeLe32 } from '../src/shared/uartV1.ts'

type TestPort = {
  port: { open(options: { baudRate: number }): Promise<void>; close(): Promise<void>; readable: ReadableStream<Uint8Array>; writable: WritableStream<Uint8Array> }
  reader: ReadableStreamDefaultController<Uint8Array>
  writes: Uint8Array[]
}

function makePort(): TestPort {
  let reader!: ReadableStreamDefaultController<Uint8Array>
  const writes: Uint8Array[] = []
  return {
    port: {
      async open(): Promise<void> {},
      async close(): Promise<void> { reader.close() },
      readable: new ReadableStream<Uint8Array>({ start(controller) { reader = controller } }),
      writable: new WritableStream<Uint8Array>({ write(frame) { writes.push(frame.slice()) } }),
    },
    reader,
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
    const data = new Uint8Array(17)
    data[0] = 0
    writeLe16(data, 1, 2)
    writeLe16(data, 3, 2)
    data[5] = 1
    data[6] = 1
    writeLe32(data, 7, 10000)
    writeLe32(data, 11, 314000)
    data[16] = 1
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request!.sequence, payload: Uint8Array.of(serviceId.getConfig, 0, ...data) }))
    const response = await responsePromise
    assert.equal(response.result, 'ok')
    assert.deepEqual(response.data, { reason: 'valid', expected_version: 2, stored_version: 2, slave_count: 1, current_sense_slave: 1, shunt_resistance_uohm: 10000, battery_capacity_mah: 314000, invert_current: false, balance_enabled: true })
    transport.disconnect()
  })
})

test('direct USB encodes balance-enabled runtime configuration', async () => {
  await withSerial(async ({ reader, writes }) => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const responsePromise = transport.request('set_config', { slave_count: 1, current_sense_slave: 1, shunt_resistance_uohm: 10000, battery_capacity_mah: 314000, invert_current: false, balance_enabled: false })
    await new Promise(resolve => setTimeout(resolve, 0))
    const decoder = new FrameDecoder()
    const request = writes.flatMap(frame => decoder.consume(frame)).find(frame => frame.type === messageType.serviceRequest)
    assert.ok(request)
    assert.deepEqual(Array.from(request!.payload), [serviceId.setConfig, 1, 1, 0x10, 0x27, 0, 0, 0x90, 0xca, 4, 0, 0, 0])
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: request!.sequence, payload: Uint8Array.of(serviceId.setConfig, 0) }))
    assert.equal((await responsePromise).result, 'ok')
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
    reader.enqueue(encodeFrame({ type: messageType.status, sequence: 0, payload: status }))
    reader.enqueue(encodeFrame({ type: messageType.hvVoltages, sequence: 0, payload: hvVoltages }))
    reader.enqueue(encodeFrame({ type: messageType.pack, sequence: 0, payload: new Uint8Array(24) }))
    reader.enqueue(encodeFrame({ type: messageType.cell, sequence: 0, payload: new Uint8Array(51) }))
    reader.enqueue(encodeFrame({ type: messageType.temperature, sequence: 0, payload: new Uint8Array(11) }))
    assert.deepEqual((await snapshot).hv_voltages, { valid: true, bat_plus_uV: 302_500_000, load_plus_uV: 1_250_000 })
    transport.disconnect()
  })
})
