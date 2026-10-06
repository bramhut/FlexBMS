import { readFileSync } from 'node:fs'
import { decodeSafetyPolicy } from '../src/shared/safetyPolicy.ts'
import assert from 'node:assert/strict'
import test from 'node:test'
import { GatewayTransport } from '../src/transports/GatewayTransport.ts'

test('Gateway serializes requests, retries Busy reads, and never replays commands', async () => {
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
    reply(result: string, data?: unknown) {
      const request = this.sent.at(-1)
      this.onmessage?.({ data: JSON.stringify({ v: 1, type: 'service_result', request_id: request.request_id, service: request.service, result, data }) })
    }
  }
  const originals = ['window', 'location', 'WebSocket'].map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)] as const)
  Object.defineProperty(globalThis, 'window', { configurable: true, value: { setTimeout, clearTimeout } })
  Object.defineProperty(globalThis, 'location', { configurable: true, value: { protocol: 'http:', host: 'test' } })
  Object.defineProperty(globalThis, 'WebSocket', { configurable: true, value: Socket })
  const transport = new GatewayTransport()
  const tick = () => new Promise(resolve => setTimeout(resolve, 0))
  try {
    await transport.connect()
    const socket = Socket.last
    socket.onopen?.()
    const first = transport.request('get_device_info', {})
    const second = transport.request('get_config', {})
    await tick()
    assert.equal(socket.sent.length, 1)
    socket.reply('busy')
    await new Promise(resolve => setTimeout(resolve, 280))
    assert.equal(socket.sent.length, 2)
    assert.equal(socket.sent[1].service, 'get_device_info')
    socket.reply('ok')
    assert.equal((await first).result, 'ok')
    await tick()
    assert.equal(socket.sent[2].service, 'get_config')
    socket.reply('ok')
    await second
    const command = transport.request('set_run_request', { requested: true })
    await tick()
    socket.reply('busy')
    assert.equal((await command).result, 'busy')
    assert.equal(socket.sent.length, 4)
    const failedRead = transport.request('get_config', {})
    await tick()
    socket.reply('transport_error')
    assert.equal((await failedRead).result, 'transport_error')
    assert.equal(socket.sent.length, 5)
    const busyRead = transport.request('get_device_info', {})
    await tick()
    for (let attempt = 0; attempt < 3; ++attempt) {
      socket.reply('busy')
      await new Promise(resolve => setTimeout(resolve, 250 * (attempt + 1) + 30))
    }
    socket.reply('busy')
    assert.equal((await busyRead).result, 'busy')
    assert.equal(socket.sent.length, 9)
    const active = transport.request('get_device_info', {})
    const queued = transport.request('get_config', {})
    await tick()
    transport.disconnect()
    assert.equal((await active).result, 'transport_error')
    assert.equal((await queued).result, 'transport_error')
    assert.equal(socket.sent.length, 10)
    await transport.connect()
    Socket.last.onopen?.()
    await tick()
    assert.equal(Socket.last.sent.length, 0, 'old requests are not replayed on reconnect')
    const safety = transport.request('get_safety_limits', {})
    await tick()
    Socket.last.reply('busy')
    await new Promise(resolve => setTimeout(resolve, 280))
    assert.equal(Socket.last.sent.length, 2)
    const bytes = JSON.parse(readFileSync(new URL('./fixtures/safety-policy.json', import.meta.url), 'utf8'))
    Socket.last.reply('ok', { safety_limits_payload: bytes })
    assert.equal(decodeSafetyPolicy((await safety).data?.safety_limits_payload)?.capacity_mah, 314000)
  } finally {
    transport.disconnect()
    for (const [key, descriptor] of originals) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor)
      else Reflect.deleteProperty(globalThis, key)
    }
  }
})
