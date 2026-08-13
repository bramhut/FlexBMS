import type { BmsTransport, Capabilities, ConnectionState, ServiceArguments, ServiceName, ServiceResponse, Snapshot, WifiConfigurationResponse, WifiScanResponse } from './Transport'
import { unavailableCapabilities } from './Transport'

type SerialPortLike = { open(options: { baudRate: number }): Promise<void>; close(): Promise<void>; readable?: ReadableStream<Uint8Array>; writable?: WritableStream<Uint8Array> }
declare global { interface Navigator { serial?: { requestPort(): Promise<SerialPortLike> } } }

export class WebSerialTransport implements BmsTransport {
  readonly label = 'Direct USB to STM32'
  private port: SerialPortLike | null = null
  private state: ConnectionState = 'disconnected'
  private stateListeners = new Set<(state: ConnectionState) => void>()
  private rawListeners = new Set<(line: string) => void>()
  private snapshotListeners = new Set<(snapshot: Snapshot) => void>()
  private eventListeners = new Set<(event: { event_id: number; value: number }) => void>()
  private pending = new Map<string, { service: ServiceName; resolve: (response: ServiceResponse) => void }>()
  private legacyStatus: Snapshot['status'] = { bms_state: 0, hv_state: 0, flags: 0, slave_count: 0, bms_active_faults: 0, bms_latched_faults: 0, hv_active_faults: 0, hv_latched_faults: 0, uptime_ms: 0, measurements_fresh: false, run_request: false }
  private legacyPack: Snapshot['pack'] = { pack_voltage_uV: 0, pack_current_raw: 0, soc_raw: 0, min_cell_uV: 0, max_cell_uV: 0, min_ntc_raw: 0, max_ntc_raw: 0, min_ic_raw: 0, max_ic_raw: 0 }
  private legacyCells = new Map<number, Snapshot['cells'][number]>()
  private legacyTemperatures = new Map<number, Snapshot['temperatures'][number]>()
  private lastRegister: ServiceResponse['data'] | undefined
  private readonly capabilities: Capabilities = { ...unavailableCapabilities(), raw_terminal: true }
  async connect(): Promise<void> {
    if (!navigator.serial) throw new Error('Web Serial is unavailable in this browser.')
    this.setState('connecting'); this.port = await navigator.serial.requestPort(); await this.port.open({ baudRate: 115200 }); this.setState('connected'); void this.readLines()
  }
  disconnect(): void { const port = this.port; this.port = null; if (port) void port.close(); this.setState('disconnected') }
  getConnectionState(): ConnectionState { return this.state }
  getCapabilities(): Capabilities { return this.capabilities }
  async configureWifi(_ssid: string, _password: string): Promise<WifiConfigurationResponse> { return { request_id: '', result: 'transport_error' } }
  async scanWifi(): Promise<WifiScanResponse> { return { request_id: '', result: 'unavailable' } }
  onSnapshot(listener: (snapshot: Snapshot) => void): () => void { this.snapshotListeners.add(listener); return () => this.snapshotListeners.delete(listener) }
  onEvent(listener: (event: { event_id: number; value: number }) => void): () => void { this.eventListeners.add(listener); return () => this.eventListeners.delete(listener) }
  onState(listener: (state: ConnectionState) => void): () => void { this.stateListeners.add(listener); return () => this.stateListeners.delete(listener) }
  onRaw(listener: (line: string) => void): () => void { this.rawListeners.add(listener); return () => this.rawListeners.delete(listener) }
  async sendRaw(line: string): Promise<void> { await this.write(`${line.endsWith('\n') ? line : `${line}\n`}`) }
  async request<S extends ServiceName>(service: S, args: ServiceArguments[S]): Promise<ServiceResponse> {
    const request_id = crypto.randomUUID().slice(0, 32)
    let line = ''
    if (service === 'set_run_request') line = `*!1E${(args as ServiceArguments['set_run_request']).requested ? '1' : '0'}`
    else if (service === 'clear_faults') line = '*!1B'
    else if (service === 'set_rtc') line = `*!19${(args as ServiceArguments['set_rtc']).unix_time_s}`
    else { const register = args as ServiceArguments['read_register']; line = `*!15${(register.slave_index + 1).toString().padStart(2, '0')}${register.register.toString(16).padStart(2, '0')}` }
    return new Promise(async resolve => { this.pending.set(service, { service, resolve }); try { await this.write(`${line}\n`) } catch { this.pending.delete(service); resolve({ request_id, service, result: 'transport_error' }) } })
  }
  private async write(line: string): Promise<void> { if (!this.port?.writable) throw new Error('USB is disconnected.'); const writer = this.port.writable.getWriter(); try { await writer.write(new TextEncoder().encode(line)) } finally { writer.releaseLock() } }
  private async readLines(): Promise<void> { if (!this.port?.readable) return; const reader = this.port.readable.getReader(); const decoder = new TextDecoder(); let buffer = ''; try { while (this.port && true) { const { value, done } = await reader.read(); if (done) break; buffer += decoder.decode(value, { stream: true }); const lines = buffer.split('\n'); buffer = lines.pop() ?? ''; lines.forEach(line => this.handleLine(line)) } } finally { reader.releaseLock(); this.setState('disconnected') } }
  private handleLine(line: string): void {
    this.rawListeners.forEach(listener => listener(line))
    if (/^\*!15[0-9A-F]{8}$/i.test(line)) { const cid = Number.parseInt(line.slice(4, 6), 10); this.lastRegister = { slave_index: cid - 1, register: Number.parseInt(line.slice(6, 8), 16), value: Number.parseInt(line.slice(8, 12), 16) }; return }
    if (line.startsWith('*!12') && line.length >= 20) { this.legacyPack.pack_voltage_uV = Number.parseInt(line.slice(4, 12), 16); const current = Number.parseInt(line.slice(12, 16), 16); this.legacyPack.pack_current_raw = current > 0x7fff ? current - 0x10000 : current; this.legacyPack.soc_raw = Number.parseInt(line.slice(-5, -1), 16); return }
    if (line.startsWith('*!13')) { const slave = Number.parseInt(line.slice(4, 6), 10); const readings: number[] = []; let balance = 0; for (let offset = 6, index = 0; offset + 9 <= line.length - 1; offset += 9, index += 1) { readings.push(Number.parseInt(line.slice(offset, offset + 8), 16)); if (line[offset + 8] === '1') balance |= 1 << index } this.legacyCells.set(slave, { slave_index: slave, balance_mask: balance, cell_voltage_uV: readings }); this.legacyStatus.slave_count = this.legacyCells.size; return }
    if (line.startsWith('*!14')) { const slave = Number.parseInt(line.slice(4, 6), 10); const values: number[] = []; for (let offset = 6; offset + 4 <= line.length - 1; offset += 4) values.push(Number.parseInt(line.slice(offset, offset + 4), 16)); this.legacyTemperatures.set(slave, { slave_index: slave, ntc_raw: values.slice(0, 4), ic_temp_raw: 0 }); return }
    if (/^\*!17[0-9A-F]{5}$/i.test(line)) { this.legacyStatus.bms_state = Number.parseInt(line.slice(4, 5), 16); this.legacyStatus.bms_active_faults = Number.parseInt(line.slice(5, 9), 16); this.legacyStatus.bms_latched_faults = this.legacyStatus.bms_active_faults; return }
    if (/^\*!1C[0-9A-F]{10}$/i.test(line)) { this.legacyStatus.hv_state = Number.parseInt(line.slice(4, 5), 16); this.legacyStatus.hv_active_faults = Number.parseInt(line.slice(5, 6), 16); this.legacyStatus.hv_latched_faults = this.legacyStatus.hv_active_faults; return }
    if (line === '*!1A') { this.legacyStatus.measurements_fresh = true; this.legacyStatus.flags |= 1 << 3; this.snapshotListeners.forEach(listener => listener({ status: { ...this.legacyStatus }, pack: { ...this.legacyPack }, cells: [...this.legacyCells.values()].sort((a, b) => a.slave_index - b.slave_index), temperatures: [...this.legacyTemperatures.values()].sort((a, b) => a.slave_index - b.slave_index) })); return }
    const match = /^\*!1F([0-9A-F]{2})([0-2])$/i.exec(line); if (!match) return; const services: Record<string, ServiceName> = { '15': 'read_register', '19': 'set_rtc', '1B': 'clear_faults', '1E': 'set_run_request' }; const service = services[match[1].toUpperCase()]; const pending = service ? this.pending.get(service) : undefined; if (pending) { this.pending.delete(service); pending.resolve({ request_id: '', service, result: ['ok', 'denied', 'invalid'][Number(match[2])] as ServiceResponse['result'], ...(service === 'read_register' && match[2] === '0' && this.lastRegister ? { data: this.lastRegister } : {}) }); this.lastRegister = undefined }
  }
  private setState(state: ConnectionState): void { this.state = state; this.stateListeners.forEach(listener => listener(state)) }
}
