import { readFile, writeFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const currentDir = path.dirname(fileURLToPath(import.meta.url))
const packageJsonPath = path.resolve(currentDir, '..', 'package.json')
const manifestPath = path.resolve(currentDir, '..', 'public', 'manifest.json')

const packageJson = JSON.parse(await readFile(packageJsonPath, 'utf8'))
const manifest = JSON.parse(await readFile(manifestPath, 'utf8'))

if (manifest.version !== packageJson.version) {
  manifest.version = packageJson.version
  await writeFile(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`, 'utf8')
  console.log(`Synchronized manifest version to ${packageJson.version}`)
}