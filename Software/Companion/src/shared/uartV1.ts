import type { Snapshot, Status } from '@/transports/Transport'

export const uartV1 = { magic0: 0x46, magic1: 0x42, version: 1, headerBytes: 7, crcBytes: 4, maxPayloadBytes: 512 } as const

export const messageType = {
  heartbeat: 0x01,
  status: 0x02,
  pack: 0x03,
  cell: 0x04,
  temperature: 0x05,
  serviceRequest: 0x10,
  serviceResponse: 0x11,
  event: 0x12,
} as const

export const serviceId = { getStatus: 0x01, setRunRequest: 0x02, clearFaults: 0x03, readRegister: 0x04, setRtc: 0x05, getDeviceInfo: 0x06, enterStm32Bootloader: 0x07, getRtc: 0x08 } as const

export type UartV1Frame = { type: number; sequence: number; payload: Uint8Array }

export function readLe16(bytes: Uint8Array, offset = 0): number { return bytes[offset] | (bytes[offset + 1] << 8) }
export function readLe32(bytes: Uint8Array, offset = 0): number { return (bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24)) >>> 0 }
export function writeLe16(bytes: Uint8Array, offset: number, value: number): void { bytes[offset] = value; bytes[offset + 1] = value >>> 8 }
export function writeLe32(bytes: Uint8Array, offset: number, value: number): void { bytes[offset] = value; bytes[offset + 1] = value >>> 8; bytes[offset + 2] = value >>> 16; bytes[offset + 3] = value >>> 24 }

export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff
  for (const byte of bytes) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit += 1) crc = (crc & 1) !== 0 ? (crc >>> 1) ^ 0xedb88320 : crc >>> 1
  }
  return (crc ^ 0xffffffff) >>> 0
}

export function encodeFrame(frame: UartV1Frame): Uint8Array {
  if (frame.payload.length > uartV1.maxPayloadBytes) throw new Error('UART v1 payload exceeds 512 bytes.')
  const encoded = new Uint8Array(uartV1.headerBytes + frame.payload.length + uartV1.crcBytes)
  encoded[0] = uartV1.magic0; encoded[1] = uartV1.magic1; encoded[2] = uartV1.version; encoded[3] = frame.type; encoded[4] = frame.sequence
  writeLe16(encoded, 5, frame.payload.length)
  encoded.set(frame.payload, uartV1.headerBytes)
  writeLe32(encoded, uartV1.headerBytes + frame.payload.length, crc32(encoded.subarray(0, uartV1.headerBytes + frame.payload.length)))
  return encoded
}

export class FrameDecoder {
  private bytes: number[] = []

  consume(chunk: Uint8Array): UartV1Frame[] {
    this.bytes.push(...chunk)
    const frames: UartV1Frame[] = []
    while (this.bytes.length >= 2) {
      const magic = this.bytes.findIndex((value, index) => value === uartV1.magic0 && this.bytes[index + 1] === uartV1.magic1)
      if (magic < 0) { this.bytes = this.bytes.length > 0 && this.bytes[this.bytes.length - 1] === uartV1.magic0 ? [uartV1.magic0] : []; return frames }
      if (magic > 0) this.bytes.splice(0, magic)
      if (this.bytes.length < uartV1.headerBytes) return frames
      const payloadLength = this.bytes[5] | (this.bytes[6] << 8)
      if (this.bytes[2] !== uartV1.version || payloadLength > uartV1.maxPayloadBytes) { this.bytes.shift(); continue }
      const total = uartV1.headerBytes + payloadLength + uartV1.crcBytes
      if (this.bytes.length < total) return frames
      const body = Uint8Array.from(this.bytes.slice(0, total - uartV1.crcBytes))
      const receivedCrc = readLe32(Uint8Array.from(this.bytes.slice(total - uartV1.crcBytes, total)))
      if (crc32(body) !== receivedCrc) { this.bytes.shift(); continue }
      frames.push({ type: this.bytes[3], sequence: this.bytes[4], payload: body.slice(uartV1.headerBytes) })
      this.bytes.splice(0, total)
    }
    return frames
  }
}

export function decodeStatus(payload: Uint8Array): Status | undefined {
  if (payload.length !== 17) return undefined
  const flags = readLe16(payload, 2)
  return { bms_state: payload[0], hv_state: payload[1], flags, slave_count: payload[4], bms_active_faults: readLe16(payload, 5), bms_latched_faults: readLe16(payload, 7), hv_active_faults: readLe16(payload, 9), hv_latched_faults: readLe16(payload, 11), uptime_ms: readLe32(payload, 13), measurements_fresh: (flags & (1 << 3)) !== 0, run_request: (flags & (1 << 2)) !== 0 }
}

export function decodePack(payload: Uint8Array): Snapshot['pack'] | undefined {
  if (payload.length !== 24) return undefined
  const current = readLe16(payload, 4)
  return { pack_voltage_uV: readLe32(payload), pack_current_raw: current > 0x7fff ? current - 0x10000 : current, soc_raw: readLe16(payload, 6), min_cell_uV: readLe32(payload, 8), max_cell_uV: readLe32(payload, 12), min_ntc_raw: readLe16(payload, 16), max_ntc_raw: readLe16(payload, 18), min_ic_raw: readLe16(payload, 20), max_ic_raw: readLe16(payload, 22) }
}

export function decodeCell(payload: Uint8Array): Snapshot['cells'][number] | undefined {
  if (payload.length !== 51) return undefined
  return { slave_index: payload[0], balance_mask: readLe16(payload, 1), cell_voltage_uV: Array.from({ length: 12 }, (_, index) => readLe32(payload, 3 + index * 4)) }
}

export function decodeTemperature(payload: Uint8Array): Snapshot['temperatures'][number] | undefined {
  if (payload.length !== 11) return undefined
  return { slave_index: payload[0], ntc_raw: Array.from({ length: 4 }, (_, index) => readLe16(payload, 1 + index * 2)), ic_temp_raw: readLe16(payload, 9) }
}
