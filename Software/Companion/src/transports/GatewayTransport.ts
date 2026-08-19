import { reconnectDelayMs } from '@/shared/reconnect'
import type { BmsTransport, Capabilities, ConnectionState, GatewayStatus, MqttConfigurationResponse, ServiceArguments, ServiceName, ServiceResponse, Snapshot, Status, WifiConfigurationResponse, WifiScanResponse } from './Transport'
import { unavailableCapabilities } from './Transport'

type Listener<T> = (value: T) => void
type Pending = { service: ServiceName; resolve: (result: ServiceResponse) => void; reject: (reason: Error) => void }
type PendingWifiConfiguration = { resolve: (result: WifiConfigurationResponse) => void }
type PendingWifiScan = { resolve: (result: WifiScanResponse) => void }
type PendingMqttConfiguration = { resolve: (result: MqttConfigurationResponse) => void }

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
  private pendingMqttConfiguration = new Map<string, PendingMqttConfiguration>()
  private nextRequestId = 1
  private reconnectAttempt = 0
  private reconnectTimer: number | null = null
  private explicitlyDisconnected = false
  private gatewayStatus: GatewayStatus | undefined
  private loadedGatewayVersion: string | undefined
  private loadedGatewayBuildId: string | undefined
  private lastBmsStatus: Status | undefined

  async connect(): Promise<void> {
    this.explicitlyDisconnected = false
    if (this.reconnectTimer !== null) { window.clearTimeout(this.reconnectTimer); this.reconnectTimer = null }
    if (this.socket !== null) return
    this.open()
  }
  disconnect(): void { this.explicitlyDisconnected = true; if (this.reconnectTimer !== null) { window.clearTimeout(this.reconnectTimer); this.reconnectTimer = null }; this.socket?.close(); this.setState('disconnected') }
  getConnectionState(): ConnectionState { return this.state }
  getCapabilities(): Capabilities { return this.capabilities }
  onBmsStatus(listener: Listener<Status>): () => void { this.statusListeners.add(listener); return () => this.statusListeners.delete(listener) }
  onSnapshot(listener: Listener<Snapshot>): () => void { this.snapshotListeners.add(listener); return () => this.snapshotListeners.delete(listener) }
  onEvent(listener: Listener<{ event_id: number; value: number; gateway_uptime_ms?: number }>): () => void { this.eventListeners.add(listener); return () => this.eventListeners.delete(listener) }
  onState(listener: (state: ConnectionState, gateway?: GatewayStatus) => void): () => void { const wrapped: Listener<[ConnectionState, GatewayStatus | undefined]> = ([state, gateway]) => listener(state, gateway); this.stateListeners.add(wrapped); return () => this.stateListeners.delete(wrapped) }
  request<S extends ServiceName>(service: S, args: ServiceArguments[S]): Promise<ServiceResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', service, result: 'transport_error' })
    const request_id = this.allocateRequestId()
    return new Promise((resolve, reject) => {
      this.pending.set(request_id, { service, resolve, reject })
      this.socket?.send(JSON.stringify({ v: 1, type: 'service', request_id, service, arguments: args }))
      window.setTimeout(() => { const pending = this.pending.get(request_id); if (pending) { this.pending.delete(request_id); pending.resolve({ request_id, service, result: 'transport_error' }) } }, 5000)
    })
  }
  configureWifi(ssid: string, password: string): Promise<WifiConfigurationResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', result: 'transport_error' })
    const request_id = this.allocateRequestId()
    return new Promise(resolve => {
      this.pendingWifiConfiguration.set(request_id, { resolve })
      this.socket?.send(JSON.stringify({ v: 1, type: 'wifi_configure', request_id, ssid, password }))
      window.setTimeout(() => { const pending = this.pendingWifiConfiguration.get(request_id); if (pending) { this.pendingWifiConfiguration.delete(request_id); pending.resolve({ request_id, result: 'transport_error' }) } }, 3000)
    })
  }
  configureMqtt(host: string, port: number, username: string, password: string): Promise<MqttConfigurationResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', result: 'transport_error' })
    const request_id = this.allocateRequestId()
    return new Promise(resolve => {
      this.pendingMqttConfiguration.set(request_id, { resolve })
      this.socket?.send(JSON.stringify({ v: 1, type: 'mqtt_configure', request_id, host, port, username, password }))
      window.setTimeout(() => { const pending = this.pendingMqttConfiguration.get(request_id); if (pending) { this.pendingMqttConfiguration.delete(request_id); pending.resolve({ request_id, result: 'transport_error' }) } }, 3000)
    })
  }
  scanWifi(): Promise<WifiScanResponse> {
    if (this.socket?.readyState !== WebSocket.OPEN) return Promise.resolve({ request_id: '', result: 'transport_error' })
    const request_id = this.allocateRequestId()
    return new Promise(resolve => {
      this.pendingWifiScan.set(request_id, { resolve })
      this.socket?.send(JSON.stringify({ v: 1, type: 'wifi_scan', request_id }))
      window.setTimeout(() => { const pending = this.pendingWifiScan.get(request_id); if (pending) { this.pendingWifiScan.delete(request_id); pending.resolve({ request_id, result: 'transport_error' }) } }, 15000)
    })
  }
  private open(): void {
    if (this.socket !== null) return
    this.setState('connecting')
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'
    const socket = new WebSocket(`${protocol}//${location.host}/ws`)
    this.socket = socket
    socket.onopen = () => { if (this.socket === socket) { this.reconnectAttempt = 0; this.setState('connected') } }
    socket.onmessage = (message) => { if (this.socket === socket) this.handleMessage(message) }
    socket.onerror = () => socket.close()
    socket.onclose = () => {
      // A superseded socket may finish closing after its replacement has
      // connected. It must never tear down that replacement or schedule a
      // second reconnect loop.
      if (this.socket !== socket) return
      this.socket = null
      this.setState('disconnected')
      for (const pending of this.pending.values()) pending.resolve({ request_id: '', service: pending.service, result: 'transport_error' })
      this.pending.clear()
      for (const pending of this.pendingWifiConfiguration.values()) pending.resolve({ request_id: '', result: 'transport_error' })
      this.pendingWifiConfiguration.clear()
      for (const pending of this.pendingWifiScan.values()) pending.resolve({ request_id: '', result: 'transport_error' })
      this.pendingWifiScan.clear()
      for (const pending of this.pendingMqttConfiguration.values()) pending.resolve({ request_id: '', result: 'transport_error' })
      this.pendingMqttConfiguration.clear()
      if (!this.explicitlyDisconnected) this.scheduleReconnect()
    }
  }
  private handleMessage(message: MessageEvent): void {
    if (typeof message.data !== 'string') return
    let parsed: Record<string, unknown>
    try { parsed = JSON.parse(message.data) as Record<string, unknown> } catch { return }
    if (parsed.v !== 1 || typeof parsed.type !== 'string') return
    if (parsed.type === 'hello') {
      const gatewayVersion = typeof parsed.gateway_version === 'string' ? parsed.gateway_version : undefined
      const gatewayBuildId = typeof parsed.gateway_build_id === 'string' ? parsed.gateway_build_id : undefined
      // gateway_version has a SemVer build suffix for older Companions; new
      // ones also use the explicit content-derived id. Both paths reload the
      // non-cacheable HTML shell without a Shift+F5 cache bypass.
      if ((gatewayBuildId && this.loadedGatewayBuildId && gatewayBuildId !== this.loadedGatewayBuildId) ||
          (!gatewayBuildId && gatewayVersion && this.loadedGatewayVersion && gatewayVersion !== this.loadedGatewayVersion)) { window.location.reload(); return }
      this.loadedGatewayVersion = gatewayVersion ?? this.loadedGatewayVersion
      this.loadedGatewayBuildId = gatewayBuildId ?? this.loadedGatewayBuildId
      this.capabilities = parsed.capabilities as Capabilities
      this.gatewayStatus = { ...(parsed.gateway_status as GatewayStatus), gateway_version: gatewayVersion, gateway_build_id: gatewayBuildId }
      this.emitState(this.gatewayStatus)
      return
    }
    if (parsed.type === 'bms_status') this.publishBmsStatus(parsed.status as Status)
    if (parsed.type === 'snapshot') { const snapshot = { status: this.normaliseBmsStatus(parsed.status as Status), pack: parsed.pack, ...(parsed.hv_voltages ? { hv_voltages: parsed.hv_voltages } : {}), cells: parsed.cells, temperatures: parsed.temperatures } as Snapshot; this.lastBmsStatus = snapshot.status; this.statusListeners.forEach(listener => listener(snapshot.status)); this.snapshotListeners.forEach(listener => listener(snapshot)) }
    if (parsed.type === 'event') this.eventListeners.forEach(listener => listener(parsed as never))
    if (parsed.type === 'gateway_status') { this.gatewayStatus = { ...this.gatewayStatus, ...(parsed as unknown as GatewayStatus) }; this.emitState(this.gatewayStatus); if (this.gatewayStatus.uart_state !== 'healthy' && this.lastBmsStatus?.measurements_fresh) this.publishBmsStatus(this.lastBmsStatus) }
    if (parsed.type === 'service_result' && typeof parsed.request_id === 'string') { const pending = this.pending.get(parsed.request_id); if (pending) { this.pending.delete(parsed.request_id); pending.resolve(parsed as unknown as ServiceResponse) } }
    if (parsed.type === 'wifi_configuration_result' && typeof parsed.request_id === 'string') { const pending = this.pendingWifiConfiguration.get(parsed.request_id); if (pending) { this.pendingWifiConfiguration.delete(parsed.request_id); pending.resolve(parsed as unknown as WifiConfigurationResponse) } }
    if (parsed.type === 'wifi_scan_result' && typeof parsed.request_id === 'string') { const pending = this.pendingWifiScan.get(parsed.request_id); if (pending) { this.pendingWifiScan.delete(parsed.request_id); pending.resolve(parsed as unknown as WifiScanResponse) } }
    if (parsed.type === 'mqtt_configuration_result' && typeof parsed.request_id === 'string') { const pending = this.pendingMqttConfiguration.get(parsed.request_id); if (pending) { this.pendingMqttConfiguration.delete(parsed.request_id); pending.resolve(parsed as unknown as MqttConfigurationResponse) } }
  }
  private scheduleReconnect(): void {
    if (this.reconnectTimer !== null) return
    const delay = reconnectDelayMs(this.reconnectAttempt++)
    this.reconnectTimer = window.setTimeout(() => { this.reconnectTimer = null; this.open() }, delay)
  }
  private allocateRequestId(): string { return `r${this.nextRequestId++}` }
  private setState(state: ConnectionState): void { this.state = state; this.emitState(undefined) }
  private emitState(gateway: GatewayStatus | undefined): void { this.stateListeners.forEach(listener => listener([this.state, gateway])) }
  private normaliseBmsStatus(status: Status): Status { return this.gatewayStatus?.uart_state === 'healthy' || this.gatewayStatus === undefined ? status : { ...status, measurements_fresh: false } }
  private publishBmsStatus(status: Status): void { this.lastBmsStatus = this.normaliseBmsStatus(status); this.statusListeners.forEach(listener => listener(this.lastBmsStatus!)) }
}
