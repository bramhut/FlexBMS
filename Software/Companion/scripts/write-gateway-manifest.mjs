import { createHash } from 'node:crypto'
import { readdir, readFile, writeFile } from 'node:fs/promises'
import { join, relative, sep } from 'node:path'

const root = new URL('../dist/gateway/', import.meta.url)
const rootPath = root.pathname.replace(/^\/(.:)/, '$1')
const files = []
async function collect(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name)
    if (entry.isDirectory()) await collect(path)
    else if (entry.name !== 'manifest.json') files.push(path)
  }
}
await collect(rootPath)
files.sort((left, right) => relative(rootPath, left).localeCompare(relative(rootPath, right)))
const listed = []
for (const path of files) {
  const data = await readFile(path)
  listed.push({ path: relative(rootPath, path).split(sep).join('/'), bytes: data.byteLength, sha256: createHash('sha256').update(data).digest('hex') })
}
const total_bytes = listed.reduce((total, file) => total + file.bytes, 0)
if (total_bytes > 512000) throw new Error(`Gateway bundle is ${total_bytes} bytes; limit is 512000 bytes.`)
const pkg = JSON.parse(await readFile(new URL('../package.json', import.meta.url)))
await writeFile(join(rootPath, 'manifest.json'), `${JSON.stringify({ format: 1, companion_version: pkg.version, files: listed, total_bytes }, null, 2)}\n`)
const mime = (path) => path.endsWith('.html') ? 'text/html' : path.endsWith('.js') ? 'application/javascript' : path.endsWith('.css') ? 'text/css' : 'application/octet-stream'
const bytes = (data) => [...data].map(value => `0x${value.toString(16).padStart(2, '0')}`).join(',')
const escaped = (value) => JSON.stringify(value)
const sourceLines = ['#include "flexbms/GatewayAssets.h"', 'namespace FlexBms::GatewayAssets {']
for (const [index, file] of listed.entries()) sourceLines.push(`static const uint8_t asset_${index}[] = {${bytes(await readFile(join(rootPath, file.path)))}};`)
sourceLines.push('const Asset kAssets[] = {')
for (const [index, file] of listed.entries()) sourceLines.push(`{${escaped(file.path)}, ${escaped(mime(file.path))}, asset_${index}, ${file.bytes}U},`)
sourceLines.push('};', `const size_t kAssetCount = ${listed.length}U;`, `const char kCompanionVersion[] = ${escaped(pkg.version)};`, '}', '')
const source = sourceLines.join('\n')
await writeFile(new URL('../../Gateway/components/gateway_api/src/GatewayAssets.cpp', import.meta.url), source)
