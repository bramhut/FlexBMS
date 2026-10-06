import assert from 'node:assert/strict'
import test from 'node:test'
import { GatewayTransport } from '../src/transports/GatewayTransport.ts'
import { unavailableCapabilities, type EnergyMeterConfiguration } from '../src/transports/Transport.ts'

test('meter configuration correlates replies, obeys capabilities and never replays after disconnect', async () => {
  class Socket {
    static OPEN = 1
    static last: Socket
    readyState = 1
    sent: any[] = []
    onopen?: () => void
    onclose?: () => void
    onmessage?: (message: { data: string }) => void
    constructor(_url: string) { Socket.last = this }
    send(data: string) { this.sent.push(JSON.parse(data)) }
    close() { this.readyState = 3; this.onclose?.() }
    message(data: unknown) { this.onmessage?.({ data: JSON.stringify(data) }) }
  }
  const originals = ['window', 'location', 'WebSocket'].map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)] as const)
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { setTimeout, clearTimeout } })
  Object.defineProperty(globalThis, 'location', { configurable: true, value: { protocol: 'http:', host: 'test' } })
  Object.defineProperty(globalThis, 'WebSocket', { configurable: true, value: Socket })
  const transport = new GatewayTransport()
  const configuration: EnergyMeterConfiguration = { baud_rate: 9600, parity: 'none', stop_bits: 1, meters: [
    { enabled: true, name: 'Schuur feeder', address: 1, reverse_power_direction: true },
    { enabled: true, name: 'Heat pump', address: 2, reverse_power_direction: false },
  ] }
  try {
    await transport.connect(); let socket = Socket.last; socket.onopen?.()
    assert.equal((await transport.configureEnergyMeters(configuration)).result, 'transport_error')
    assert.equal(socket.sent.length, 0)
    socket.message({ v: 1, type: 'hello', capabilities: { ...unavailableCapabilities(), energy_meters_configuration: true }, gateway_status: {} })
    const result = transport.configureEnergyMeters(configuration)
    const request = socket.sent[0]
    assert.equal(request.type, 'energy_meters_configure'); assert.deepEqual(request.configuration, configuration)
    socket.message({ v: 1, type: 'energy_meters_configuration_result', request_id: 'wrong', result: 'accepted' })
    socket.message({ v: 1, type: 'energy_meters_configuration_result', request_id: request.request_id, result: 'unknown' })
    socket.message({ v: 1, type: 'energy_meters_configuration_result', request_id: request.request_id, result: 'invalid' })
    assert.equal((await result).result, 'invalid')
    const pending = transport.configureEnergyMeters(configuration)
    transport.disconnect(); assert.equal((await pending).result, 'transport_error')
    await transport.connect(); socket = Socket.last; socket.onopen?.()
    assert.equal(socket.sent.length, 0)
    socket.message({ v: 1, type: 'hello', capabilities: unavailableCapabilities(), gateway_status: {} })
    assert.equal((await transport.configureEnergyMeters(configuration)).result, 'transport_error')
    assert.equal(socket.sent.length, 0)
  } finally {
    transport.disconnect()
    for (const [key, descriptor] of originals) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor)
      else Reflect.deleteProperty(globalThis, key)
    }
  }
})
