import assert from 'node:assert/strict'
import test from 'node:test'
import { parseFirmwareBundle } from '../src/shared/firmwareBundle.ts'

function bundle(manifest: unknown, ...images: Uint8Array[]): Blob {
  const manifestBytes = new TextEncoder().encode(JSON.stringify(manifest))
  const header = new Uint8Array(8)
  header.set([0x46, 0x42, 0x55, 0x31])
  new DataView(header.buffer).setUint32(4, manifestBytes.length, true)
  return new Blob([header, manifestBytes, ...images])
}

test('firmware bundle exposes both images as byte slices', async () => {
  const stm32 = Uint8Array.of(1, 2, 3)
  const gateway = Uint8Array.of(4, 5, 6, 7)
  const manifest = { format_version: 1, version: '0.1.1', images: [
    { target: 'stm32', version: '0.1.1', image_bytes: stm32.length, image_crc32: '55BC801D' },
    { target: 'gateway', version: '0.1.1', image_bytes: gateway.length, image_crc32: '60D3B885' },
  ] }
  const parsed = await parseFirmwareBundle(bundle(manifest, stm32, gateway))
  assert.equal(parsed.version, '0.1.1')
  assert.deepEqual(parsed.images.stm32, { target: 'stm32', version: '0.1.1', image_bytes: 3, image_crc32: '55BC801D', offset: 8 + new TextEncoder().encode(JSON.stringify(manifest)).length })
  assert.equal(parsed.images.gateway.offset, parsed.images.stm32.offset + stm32.length)
})

test('firmware bundle rejects a missing target', async () => {
  const manifest = { format_version: 1, version: '0.1.1', images: [{ target: 'stm32', version: '0.1.1', image_bytes: 1, image_crc32: '00000000' }] }
  await assert.rejects(parseFirmwareBundle(bundle(manifest, Uint8Array.of(1))), /valid FlexBMS_bundle\.fbu/)
})
