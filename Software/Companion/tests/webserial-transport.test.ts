import assert from 'node:assert/strict'
import test from 'node:test'
import { WebSerialTransport } from '../src/transports/WebSerialTransport.ts'
import { encodeFrame, FrameDecoder, messageType, serviceId } from '../src/shared/uartV1.ts'

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
    const response = transport.request('clear_faults', {})
    await new Promise(resolve => setTimeout(resolve, 0))
    const decoder = new FrameDecoder()
    const requests = writes.flatMap(frame => decoder.consume(frame)).filter(frame => frame.type === messageType.serviceRequest)
    assert.equal(requests.length, 1)
    assert.equal(requests[0].sequence, 1)
    reader.enqueue(encodeFrame({ type: messageType.serviceResponse, sequence: 1, payload: Uint8Array.of(serviceId.clearFaults, 3) }))
    assert.equal((await response).result, 'busy')
    transport.disconnect()
  })
})

test('direct USB resolves pending services when the port disconnects', async () => {
  await withSerial(async () => {
    const transport = new WebSerialTransport()
    await transport.connect()
    const response = transport.request('clear_faults', {})
    transport.disconnect()
    assert.equal((await response).result, 'transport_error')
  })
})
