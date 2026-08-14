export type FirmwareTarget = 'gateway' | 'stm32'

export type FirmwareBundleImage = {
  target: FirmwareTarget
  version: string
  image_bytes: number
  image_crc32: string
  offset: number
}

export type FirmwareBundle = { version: string; images: Record<FirmwareTarget, FirmwareBundleImage> }

type ManifestImage = { target?: unknown; version?: unknown; image_bytes?: unknown; image_crc32?: unknown }
type Manifest = { format_version?: unknown; version?: unknown; images?: unknown }

const headerBytes = 8
const maxManifestBytes = 16 * 1024
const semanticVersion = /^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/

function fail(): never { throw new Error('Choose a valid FlexBMS_bundle.fbu file.') }

export async function parseFirmwareBundle(file: Blob): Promise<FirmwareBundle> {
  if (file.size < headerBytes) fail()
  const header = new Uint8Array(await file.slice(0, headerBytes).arrayBuffer())
  if (String.fromCharCode(...header.subarray(0, 4)) !== 'FBU1') fail()
  const manifestBytes = new DataView(header.buffer, header.byteOffset, header.byteLength).getUint32(4, true)
  if (manifestBytes === 0 || manifestBytes > maxManifestBytes || headerBytes + manifestBytes > file.size) fail()

  let manifest: Manifest
  try { manifest = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(await file.slice(headerBytes, headerBytes + manifestBytes).arrayBuffer())) as Manifest } catch { fail() }
  if (manifest.format_version !== 1 || typeof manifest.version !== 'string' || !semanticVersion.test(manifest.version) || !Array.isArray(manifest.images) || manifest.images.length !== 2) fail()

  let offset = headerBytes + manifestBytes
  const images: Partial<Record<FirmwareTarget, FirmwareBundleImage>> = {}
  for (const value of manifest.images as ManifestImage[]) {
    const bytes = value.image_bytes
    if ((value.target !== 'gateway' && value.target !== 'stm32') || images[value.target] || typeof value.version !== 'string' || !semanticVersion.test(value.version) ||
        typeof bytes !== 'number' || !Number.isSafeInteger(bytes) || bytes <= 0 || !/^[0-9a-fA-F]{8}$/.test(String(value.image_crc32)) || offset + bytes > file.size) fail()
    images[value.target] = { target: value.target, version: value.version, image_bytes: bytes, image_crc32: String(value.image_crc32).toUpperCase(), offset }
    offset += bytes
  }
  if (offset !== file.size || !images.gateway || !images.stm32) fail()
  return { version: manifest.version, images: { gateway: images.gateway, stm32: images.stm32 } }
}
