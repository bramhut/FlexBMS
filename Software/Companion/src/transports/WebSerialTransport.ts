import type { BmsTransport, Capabilities, ConnectionState, MqttConfigurationResponse, ServiceArguments, ServiceName, ServiceResponse, Snapshot, Status, WifiConfigurationResponse, WifiScanResponse } from './Transport.ts'
import { unavailableCapabilities } from './Transport.ts'
import { decodeCell, decodeHvVoltages, decodePack, decodeStatus, decodeTemperature, encodeFrame, FrameDecoder, messageType, readLe16, readLe32, serviceId } from '../shared/uartV1.ts'

type SerialPortLike = { open(options: { baudRate: number }): Promise<void>; close(): Promise<void>; readable?: ReadableStream<Uint8Array>; writable?: WritableStream<Uint8Array> }
declare global { interface Navigator { serial?: { requestPort(): Promise<SerialPortLike> } } }

type Pending = { service: ServiceName; resolve: (response: ServiceResponse) => void; timeout: number }
type Listener<T> = (value: T) => void

const resultByWire = ['ok', 'denied', 'invalid', 'busy'] as const

export class WebSerialTransport implements BmsTransport {
  readonly label = 'Direct USB to STM32'
  private port: SerialPortLike | null = null
  private state: ConnectionState = 'disconnected'
  private stateListeners = new Set<Listener<ConnectionState>>()
  private statusListeners = new Set<Listener<Status>>()
  private snapshotListeners = new Set<Listener<Snapshot>>()
  private eventListeners = new Set<Listener<{ event_id: number; value: number }>>()
  private pending = new Map<number, Pending>()
  private nextSequence = 1
  private heartbeatTimer: number | undefined
  private writeChain: Promise<void> = Promise.resolve()
  private decoder = new FrameDecoder()
  private status: Status | null = null
  private pack: Snapshot['pack'] | null = null
  private hvVoltages: Snapshot['hv_voltages']
  private cells = new Map<number, Snapshot['cells'][number]>()
  private temperatures = new Map<number, Snapshot['temperatures'][number]>()
  private readonly capabilities: Capabilities = unavailableCapabilities()

  async connect(): Promise<void> {
    if (!navigator.serial) throw new Error('Web Serial is unavailable in this browser.')
    if (this.port) return
    this.setState('connecting')
    const port = await navigator.serial.requestPort()
    await port.open({ baudRate: 115200 })
    this.port = port
    this.decoder = new FrameDecoder()
    this.setState('connected')
    void this.readFrames(port)
    void this.sendHeartbeat()
    this.heartbeatTimer = window.setInterval(() => { void this.sendHeartbeat() }, 500)
  }

  disconnect(): void {
    const port = this.port
    this.port = null
    this.stopHeartbeat()
    this.resolvePendingAsTransportError()
    if (port) void port.close()
    this.setState('disconnected')
  }

  getConnectionState(): ConnectionState { return this.state }
  getCapabilities(): Capabilities { return this.capabilities }
  async configureWifi(_ssid: string, _password: string): Promise<WifiConfigurationResponse> { return { request_id: '', result: 'transport_error' } }
  async configureMqtt(_host: string, _port: number, _username: string, _password: string): Promise<MqttConfigurationResponse> { return { request_id: '', result: 'transport_error' } }
  async scanWifi(): Promise<WifiScanResponse> { return { request_id: '', result: 'unavailable' } }
  onBmsStatus(listener: Listener<Status>): () => void { this.statusListeners.add(listener); return () => this.statusListeners.delete(listener) }
  onSnapshot(listener: Listener<Snapshot>): () => void { this.snapshotListeners.add(listener); return () => this.snapshotListeners.delete(listener) }
  onEvent(listener: Listener<{ event_id: number; value: number }>): () => void { this.eventListeners.add(listener); return () => this.eventListeners.delete(listener) }
  onState(listener: Listener<ConnectionState>): () => void { this.stateListeners.add(listener); return () => this.stateListeners.delete(listener) }

  request<S extends ServiceName>(service: S, args: ServiceArguments[S]): Promise<ServiceResponse> {
    if (!this.port?.writable) return Promise.resolve({ request_id: '', service, result: 'transport_error' })
    const sequence = this.allocateSequence()
    const payload = this.servicePayload(service, args)
    return new Promise(resolve => {
      const timeout = window.setTimeout(() => {
        const pending = this.pending.get(sequence)
        if (!pending) return
        this.pending.delete(sequence)
        resolve({ request_id: '', service, result: 'transport_error' })
      }, 5000)
      this.pending.set(sequence, { service, resolve, timeout })
      void this.writeFrame(messageType.serviceRequest, sequence, payload).catch(() => this.completeTransportError(sequence))
    })
  }

  private servicePayload<S extends ServiceName>(service: S, args: ServiceArguments[S]): Uint8Array {
    if (service === 'set_run_request') return Uint8Array.of(serviceId.setRunRequest, (args as ServiceArguments['set_run_request']).requested ? 1 : 0)
    if (service === 'set_balancing_enabled') return Uint8Array.of(serviceId.setBalancingEnabled, (args as ServiceArguments['set_balancing_enabled']).enabled ? 1 : 0)
    if (service === 'acknowledge_faults') return Uint8Array.of(serviceId.acknowledgeFaults)
    if (service === 'get_rtc') return Uint8Array.of(serviceId.getRtc)
    if (service === 'get_device_info') return Uint8Array.of(serviceId.getDeviceInfo)
    const register = args as ServiceArguments['read_register']
    return Uint8Array.of(serviceId.readRegister, register.slave_index, register.register)
  }

  private allocateSequence(): number { const sequence = this.nextSequence; this.nextSequence = this.nextSequence === 255 ? 1 : this.nextSequence + 1; return sequence }
  private async sendHeartbeat(): Promise<void> { if (this.port?.writable) await this.writeFrame(messageType.heartbeat, 0, new Uint8Array()) }
  private async writeFrame(type: number, sequence: number, payload: Uint8Array): Promise<void> { await this.write(encodeFrame({ type, sequence, payload })) }
  private write(bytes: Uint8Array): Promise<void> {
    const port = this.port
    if (!port?.writable) return Promise.reject(new Error('USB is disconnected.'))
    const send = async (): Promise<void> => {
      if (this.port !== port || !port.writable) throw new Error('USB is disconnected.')
      const writer = port.writable.getWriter()
      try { await writer.write(bytes) } finally { writer.releaseLock() }
    }
    const queued = this.writeChain.then(send, send)
    this.writeChain = queued.catch(() => undefined)
    return queued
  }

  private async readFrames(port: SerialPortLike): Promise<void> {
    if (!port.readable) return
    const reader = port.readable.getReader()
    try {
      while (this.port === port) {
        const { value, done } = await reader.read()
        if (done) break
        if (value) for (const frame of this.decoder.consume(value)) this.handleFrame(frame.type, frame.sequence, frame.payload)
      }
    } finally {
      reader.releaseLock()
      if (this.port === port) {
        this.port = null
        this.stopHeartbeat()
        this.resolvePendingAsTransportError()
        this.setState('disconnected')
      }
    }
  }

  private handleFrame(type: number, sequence: number, payload: Uint8Array): void {
    if (type === messageType.status) {
      const status = decodeStatus(payload)
      if (!status) return
      this.status = status
      if (!status.measurements_fresh) { this.pack = null; this.hvVoltages = undefined; this.cells.clear(); this.temperatures.clear() }
      this.emitStatus()
      this.emitSnapshotIfComplete()
      return
    }
    if (type === messageType.pack) { const pack = decodePack(payload); if (pack) { this.pack = pack; this.emitSnapshotIfComplete() }; return }
    if (type === messageType.hvVoltages) { const voltages = decodeHvVoltages(payload); if (voltages) { this.hvVoltages = voltages; this.emitSnapshotIfComplete() }; return }
    if (type === messageType.cell) { const cell = decodeCell(payload); if (cell) { this.cells.set(cell.slave_index, cell); this.emitSnapshotIfComplete() }; return }
    if (type === messageType.temperature) { const temperature = decodeTemperature(payload); if (temperature) { this.temperatures.set(temperature.slave_index, temperature); this.emitSnapshotIfComplete() }; return }
    if (type === messageType.event && payload.length === 5) { this.applyEvent(payload[0], readLe32(payload, 1)); return }
    if (type === messageType.serviceResponse) this.completeService(sequence, payload)
  }

  private completeService(sequence: number, payload: Uint8Array): void {
    const pending = this.pending.get(sequence)
    if (!pending || payload.length < 2) return
    this.pending.delete(sequence)
    window.clearTimeout(pending.timeout)
    const result = resultByWire[payload[1]] ?? 'transport_error'
    let data: ServiceResponse['data']
    if (result === 'ok' && pending.service === 'read_register' && payload.length === 6) data = { slave_index: payload[2], register: payload[3], value: readLe16(payload, 4) }
    if (result === 'ok' && pending.service === 'get_rtc' && payload.length === 6) data = { unix_time_s: readLe32(payload, 2) }
    if (result === 'ok' && pending.service === 'get_device_info' && payload.length === 6) data = { firmware_version_packed: readLe32(payload, 2) }
    pending.resolve({ request_id: '', service: pending.service, result, ...(data ? { data } : {}) })
  }

  private applyEvent(event_id: number, value: number): void {
    if (this.status) {
      if (event_id === 1) this.status.bms_state = value
      else if (event_id === 2) this.status.hv_state = value
      else if (event_id === 3) this.status.bms_active_errors = value
      else if (event_id === 4) this.status.bms_latched_errors = value
      else if (event_id === 5) this.status.hv_active_errors = value
      else if (event_id === 6) this.status.hv_latched_errors = value
      else if (event_id === 7) this.status.warnings = value
      else if (event_id === 8) { this.status.measurements_fresh = value !== 0; if (value === 0) { this.pack = null; this.cells.clear(); this.temperatures.clear() } }
      this.emitStatus()
    }
    this.eventListeners.forEach(listener => listener({ event_id, value }))
  }

  private emitStatus(): void { if (this.status) this.statusListeners.forEach(listener => listener({ ...this.status! })) }
  private emitSnapshotIfComplete(): void {
    if (!this.status?.measurements_fresh || !this.pack || this.status.slave_count === 0) return
    for (let slave = 0; slave < this.status.slave_count; slave += 1) if (!this.cells.has(slave) || !this.temperatures.has(slave)) return
    this.snapshotListeners.forEach(listener => listener({ status: { ...this.status! }, pack: { ...this.pack! }, ...(this.hvVoltages ? { hv_voltages: { ...this.hvVoltages } } : {}), cells: Array.from(this.cells.values()).sort((a, b) => a.slave_index - b.slave_index), temperatures: Array.from(this.temperatures.values()).sort((a, b) => a.slave_index - b.slave_index) }))
  }

  private completeTransportError(sequence: number): void { const pending = this.pending.get(sequence); if (!pending) return; this.pending.delete(sequence); window.clearTimeout(pending.timeout); pending.resolve({ request_id: '', service: pending.service, result: 'transport_error' }) }
  private resolvePendingAsTransportError(): void { for (const [sequence] of this.pending) this.completeTransportError(sequence) }
  private stopHeartbeat(): void { if (this.heartbeatTimer !== undefined) { window.clearInterval(this.heartbeatTimer); this.heartbeatTimer = undefined } }
  private setState(state: ConnectionState): void { this.state = state; this.stateListeners.forEach(listener => listener(state)) }
}
