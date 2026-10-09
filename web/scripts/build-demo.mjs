import { mkdir, writeFile, readFile, copyFile, cp, rm } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const output = path.join(root, 'web/site-dist');
const generated = path.join(root, 'web/.wasm-build');
await mkdir(output, { recursive: true });
await mkdir(generated, { recursive: true });
await mkdir(path.join(output, 'assets'), { recursive: true });
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' });
  if (result.error || result.status !== 0) throw result.error || new Error(`${command} failed`);
}
run('npm', ['--prefix', 'web', 'run', 'build']);
await writeFile(path.join(generated, 'DataPaths.hpp'), '#pragma once\n#define QM_SOURCE_DATA_DIR "/data"\n#define QM_INSTALL_DATA_DIR "/data"\n#define QM_INSTALL_DATA_FROM_BIN "data"\n');
const sources = ['WasmBindings', 'ApiSession', 'Superposition', 'OrbitalModel', 'Simulation', 'Element', 'QuantumMath', 'Orbital', 'RadialSampler', 'ComputeBackend', 'ComputeDispatch'].map(name => `src/${name}.cpp`);
run(process.env.EMXX || 'em++', [...sources, '-std=c++17', '-O2', '-fexceptions', '--bind', '-Iinclude', `-I${generated}`, '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,worker', '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=67108864', '-sMAXIMUM_MEMORY=268435456', '--preload-file', 'data/elements.json@/data/elements.json', '-o', path.join(output, 'assets/physics.mjs')]);
await cp(path.join(root, 'web/dist'), path.join(output, 'assets'), { recursive: true });
const html = (await readFile(path.join(root, 'web/index.html'), 'utf8')).replace('name="execution-mode" content="native"', 'name="execution-mode" content="wasm"');
await writeFile(path.join(output, 'index.html'), html);
await copyFile(path.join(root, 'web/style.css'), path.join(output, 'style.css'));
await rm(path.join(output, '_headers'), { force: true });
console.log(`Public browser demo built at ${output}`);
