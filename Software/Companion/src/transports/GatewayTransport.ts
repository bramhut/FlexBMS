import { reconnectDelayMs } from '@/shared/reconnect'
import type { BmsTransport, Capabilities, ConnectionState, GatewayStatus, ServiceArguments, ServiceName, ServiceResponse, Snapshot, Status, WifiConfigurationResponse, WifiScanResponse } from './Transport'
import { unavailableCapabilities } from './Transport'

type Listener<T> = (value: T) => void
type Pending = { service: ServiceName; resolve: (result: ServiceResponse) => void; reject: (reason: Error) => void }
type PendingWifiConfiguration = { resolve: (result: WifiConfigurationResponse) => void }
type PendingWifiScan = { resolve: (result: WifiScanResponse) => void }

export class GatewayTransport implements BmsTransport {
  readonly label = 'Via ESP32 Gateway'
  private socket: WebSocket | null = null
  private state: ConnectionState = 'disconnected'
  private capabilities: Capabilities = unavailableCapabilities()
  private statusListeners = new Set<Listener<Status>>()
  private snapshotListeners = new Set<Listener<Snapshot>>()
  private eventListeners = new Set<Listener<{ event_id: number; value: number; gateway_uptime_ms?: number }>>()
  private stateListeners = new Set<Listener<[ConnectionState, GatewayStatus | undefined]>>()
  private pending = new Map<string, Pending>()
  private pendingWifiConfiguration = new Map<string, PendingWifiConfiguration>()
  private pendingWifiScan = new Map<string, PendingWifiScan>()
  private reconnectAttempt = 0
  private reconnectTimer: number | null = null
  private explicitlyDisconnected = false

  async connect(): Promise<void> {
    this.explicitlyDisconnected = false
    if (this.socket?.readyState === WebSocket.OPEN || this.socket?.readyState === WebSocket.CONNECTING) return
    this.open()
  }
  disconnect(): void { this.explicitlyDisconnected = true; if (this.reconnectTimer !== null) window.clearTimeout(this.reconnectTimer); this.socket?.close(); this.setState('disconnected') }
  getConnectionState(): ConnectionState { return this.state }
  getCapabilities(): Capabilities { return this.capabilities }
  onBmsStatus(listener: Listener<Status>): () => void { this.statusListeners.add(listener); return () => this.statusListeners.delete(listener) }
  onSnapshot(listener: Listener<Snapshot>): () => void { this.snapshotListeners.add(listener); return () => this.snapshotListeners.delete(listener) }
  onEvent(listener: Listener<{ event_id: number; value: number; gateway_uptime_ms?: number }>): () => void { this.eventListeners.add(listener); return () => this.eventListeners.delete(listener) }
  onState(listener: (state: ConnectionState, gateway?: GatewayStatus) => void): () => void { const wrapped: Listener<[ConnectionState, GatewayStatus | undefined]> = ([state, gateway]) => listener(state, gateway); this.stateListeners.add(wrapped); return () => this.stateListeners.delete(wrapped) }
  request<S extends ServiceName>(service: S, args: ServiceArguments[S]): Promise<ServiceResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', service, result: 'transport_error' })
    const request_id = crypto.randomUUID().replace(/-/g, '').slice(0, 32)
    return new Promise((resolve, reject) => {
      this.pending.set(request_id, { service, resolve, reject })
      this.socket?.send(JSON.stringify({ v: 1, type: 'service', request_id, service, arguments: args }))
      window.setTimeout(() => { const pending = this.pending.get(request_id); if (pending) { this.pending.delete(request_id); pending.resolve({ request_id, service, result: 'transport_error' }) } }, 5000)
    })
  }
  configureWifi(ssid: string, password: string): Promise<WifiConfigurationResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', result: 'transport_error' })
    const request_id = crypto.randomUUID().replace(/-/g, '').slice(0, 32)
    return new Promise(resolve => {
      this.pendingWifiConfiguration.set(request_id, { resolve })
      this.socket?.send(JSON.stringify({ v: 1, type: 'wifi_configure', request_id, ssid, password }))
      window.setTimeout(() => { const pending = this.pendingWifiConfiguration.get(request_id); if (pending) { this.pendingWifiConfiguration.delete(request_id); pending.resolve({ request_id, result: 'transport_error' }) } }, 3000)
    })
  }
  scanWifi(): Promise<WifiScanResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', result: 'transport_error' })
    const request_id = crypto.randomUUID().replace(/-/g, '').slice(0, 32)
    return new Promise(resolve => {
      this.pendingWifiScan.set(request_id, { resolve })
      this.socket?.send(JSON.stringify({ v: 1, type: 'wifi_scan', request_id }))
      window.setTimeout(() => { const pending = this.pendingWifiScan.get(request_id); if (pending) { this.pendingWifiScan.delete(request_id); pending.resolve({ request_id, result: 'transport_error' }) } }, 15000)
    })
  }
  private open(): void {
    this.setState('connecting')
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'
    this.socket = new WebSocket(`${protocol}//${location.host}/ws`)
    this.socket.onopen = () => { this.reconnectAttempt = 0; this.setState('connected') }
    this.socket.onmessage = (message) => this.handleMessage(message)
    this.socket.onerror = () => this.socket?.close()
    this.socket.onclose = () => { this.socket = null; this.setState('disconnected'); for (const pending of this.pending.values()) pending.resolve({ request_id: '', service: pending.service, result: 'transport_error' }); this.pending.clear(); for (const pending of this.pendingWifiConfiguration.values()) pending.resolve({ request_id: '', result: 'transport_error' }); this.pendingWifiConfiguration.clear(); for (const pending of this.pendingWifiScan.values()) pending.resolve({ request_id: '', result: 'transport_error' }); this.pendingWifiScan.clear(); if (!this.explicitlyDisconnected) this.scheduleReconnect() }
  }
  private handleMessage(message: MessageEvent): void {
    if (typeof message.data !== 'string') return
    let parsed: Record<string, unknown>
    try { parsed = JSON.parse(message.data) as Record<string, unknown> } catch { return }
    if (parsed.v !== 1 || typeof parsed.type !== 'string') return
    if (parsed.type === 'hello') { this.capabilities = parsed.capabilities as Capabilities; this.emitState(parsed.gateway_status as GatewayStatus); return }
    if (parsed.type === 'bms_status') this.statusListeners.forEach(listener => listener(parsed.status as Status))
    if (parsed.type === 'snapshot') { const snapshot = { status: parsed.status, pack: parsed.pack, cells: parsed.cells, temperatures: parsed.temperatures } as Snapshot; this.statusListeners.forEach(listener => listener(snapshot.status)); this.snapshotListeners.forEach(listener => listener(snapshot)) }
    if (parsed.type === 'event') this.eventListeners.forEach(listener => listener(parsed as never))
    if (parsed.type === 'gateway_status') this.emitState(parsed as unknown as GatewayStatus)
    if (parsed.type === 'service_result' && typeof parsed.request_id === 'string') { const pending = this.pending.get(parsed.request_id); if (pending) { this.pending.delete(parsed.request_id); pending.resolve(parsed as unknown as ServiceResponse) } }
    if (parsed.type === 'wifi_configuration_result' && typeof parsed.request_id === 'string') { const pending = this.pendingWifiConfiguration.get(parsed.request_id); if (pending) { this.pendingWifiConfiguration.delete(parsed.request_id); pending.resolve(parsed as unknown as WifiConfigurationResponse) } }
    if (parsed.type === 'wifi_scan_result' && typeof parsed.request_id === 'string') { const pending = this.pendingWifiScan.get(parsed.request_id); if (pending) { this.pendingWifiScan.delete(parsed.request_id); pending.resolve(parsed as unknown as WifiScanResponse) } }
  }
  private scheduleReconnect(): void { const delay = reconnectDelayMs(this.reconnectAttempt++); this.reconnectTimer = window.setTimeout(() => this.open(), delay) }
  private setState(state: ConnectionState): void { this.state = state; this.emitState(undefined) }
  private emitState(gateway: GatewayStatus | undefined): void { this.stateListeners.forEach(listener => listener([this.state, gateway])) }
}
