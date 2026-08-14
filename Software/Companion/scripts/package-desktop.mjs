import { spawnSync } from 'node:child_process'
import { readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const packageJson = JSON.parse(await readFile(new URL('../package.json', import.meta.url)))
const output = process.env.FLEXBMS_DESKTOP_OUTPUT_DIR || resolve(`electron-dist/build-${new Date().toISOString().replace(/[:.]/g, '-')}`)
const builder = fileURLToPath(new URL('../node_modules/electron-builder/cli.js', import.meta.url))
const result = spawnSync(process.execPath, [builder, '--win', 'portable', '--x64', `--config.directories.output=${output}`], {
  stdio: 'inherit',
  env: process.env,
})
if (result.status !== 0) process.exit(result.status ?? 1)
console.log(`Portable Companion: ${resolve(output, `FlexBMS-Companion-${packageJson.version}-portable.exe`)}`)
