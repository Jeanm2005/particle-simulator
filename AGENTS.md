# AGENTS.md — collaboration notes for humans and AI agents

## Project location

- Canonical working tree: `~/havefun/atoms`, or the repository root after cloning.
- Sandbox / CI paths may use `/home/workdir/artifacts/atoms`.
- Run development checks from the repository root. Default data discovery also
  supports installed binaries and development runs from other directories.
- Read this file and README.md before large refactors.

## Project

C++17 quantum orbital visualiser with hydrogenic radial/angular wavefunctions,
CDF sampling, nuclear Z or configuration-based Slater screening, and GLFW +
OpenGL 3.3 point-cloud and fragment-shader ray-march rendering.

Element records live in `data/elements.json`. Reference inspiration:
[kavan010/Atoms](https://github.com/kavan010/Atoms).

## Requirements

| Item | Priority / status |
|------|-------------------|
| Correct Schrödinger sampling and multi-element Z | Required; implemented |
| OpenGL point cloud, camera, live n/l/m | Required; implemented |
| Probability-current animation | Required; implemented; preserve radius and Y |
| GPU ray-march path | Implemented; bounded rays, shared density scale, length-aware opacity, matched FOV; representative higher-orbital convergence/software timings verified; hardware performance pending |
| CUDA acceleration | Implemented; CUDA compilation verified in CI; NVIDIA parity/performance pending |
| Two-orbital superpositions | Implemented; shared pure-Z Hamiltonian, n <= 4, CPU density snapshots |
| Provisional browser demo | Shared C++ ApiSession compiled to WebAssembly; each tab owns its state |
| Agentic workflow helpers | Optional |
| 2D Bohr model | Out of scope |

## CUDA milestone

Implement under `cuda/`:

1. Parallel inverse-CDF or rejection sampling for large particle counts.
2. GPU probability-current updates that preserve radius and height.
3. `QM_ENABLE_CUDA` CMake option and CUDA language detection when available.
4. CPU fallback when CUDA is unavailable; retain the CPU OpenGL path.
5. Numerical parity checks and performance measurements.

CUDA kernels and `QM_ENABLE_CUDA` now exist. CPU fallback is required both at
build time and runtime. `compute_tests --cuda` runs parity checks and end-to-end
performance measurements; it skips without a usable device.
CUDA compilation and CPU fallback passed GitHub Actions in its CUDA container.
NVIDIA runtime validation is still pending: the current environment lacks nvcc
and device access.
Keep README status accurate; do not claim measured acceleration before benchmarking.

## Architecture and physics

- Physics stays in QuantumMath, RadialSampler, and Simulation; no OpenGL includes.
- Rendering stays in Engine and Camera.
- Use C++17 and CMake 3.16+; keep the modular layout.
- Use `qm::PI` from Constants.hpp; do not duplicate the constant.
- Element tables belong in JSON; the small built-in table is an emergency fallback.
- Coordinates use a0 and Y as the polar axis in both console and visual modes.
- Slater screening requires an expanded neutral configuration and an occupied
  Slater group. Unoccupied groups and unknown configurations require pure Z.
- Reject invalid inputs and nonfinite distributions before sampling.
- Preserve `QM_HAS_OPENGL` guards so console builds need no graphics dependencies.

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/atom_sim --console
# Optional when a display and graphics context are available:
./build/atom_sim --visual
```

Console smoke input: H, 1s, p. Expect finite mean radius near 1.5 a0 and
`H_1s_cloud.xyz`. Gold, 6s, s must also complete with Zeff = 3.7.

Check the CPU fallback with `-DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=ON` in a separate
build directory. Regression suites cover screening, sampling, flow, coordinates,
input parsing, relocated installations, and export failures. Offscreen shader
checks run when EGL is available. Renderer image checks cover representative states through n=7 from two angles
against a 4096-step reference. `shader_tests --benchmark` additionally reports
synchronized offscreen timings; keep software-GL and hardware measurements distinct.
Other graphics runtime checks require a working
context; report when they cannot be performed.

For WSL graphics problems, try software GL with `LIBGL_ALWAYS_SOFTWARE=1`.
For a suspected stale binary, configure a fresh build directory and rebuild.

## Workflow

- Complete, verify, and commit each agreed step; report the hash for the user to push.
- Keep commits focused and avoid unrelated rewrites.
- Keep README and these instructions synchronized when behavior changes.
- GitHub Actions builds/tests console and OpenGL paths, requires offscreen shader
  coverage, and compiles CUDA in a development container while testing CPU fallback.
- Successful `v*` tag runs publish tested Linux packages and checksums to GitHub
  Releases. GPU runtime parity and performance still require NVIDIA hardware.
- Keep `.github/workflows/ci.yml` synchronized with build and test requirements.

## File ownership

| Area | Primary files |
|------|---------------|
| Wavefunctions / current | QuantumMath.* |
| Sampling | RadialSampler.* |
| Console / XYZ | Simulation.*, main.cpp |
| Rendering / camera | Engine.*, Camera.hpp, ShaderProgram.* |
| Elements / discovery | Element.*, data/elements.json, cmake/DataPaths.hpp.in |
| CUDA | cuda/*, CMakeLists.txt |
| Regressions | tests/* |
| Local transport | src/ApiWorker.cpp, tools/serve.py |
| TypeScript UI | web/* |

## Reference mapping

| Reference idea | Implementation |
|----------------|----------------|
| Laguerre recurrence sampling | QuantumMath::associatedLaguerre, RadialSampler CDF |
| Point cloud | Engine point path |
| Raytracer | Engine::createRaytraceShaders / drawRaytrace |
| Probability flow | probabilityCurrentVelocity, advanceProbabilityCurrent |
| Hydrogen-only model | Extended through element JSON and Slater screening |

## Product direction

The intended scope is an interactive quantum/particle physics simulator with a
TypeScript UI for particle selection and state/parameter input. Atomic
orbitals are the first implemented model, not the full product scope. Expand via
validated models with explicit observables, interactions, and validity limits;
do not present arbitrary particle shapes as physical predictions.

After the CUDA and rendering milestones, track model/backend interfaces, the
TypeScript UI, broader particle models, orbital density sonification and WAV
export, time-dependent superpositions, and a possible lattice/phonon extension.
The scope milestone is documented in `docs/model-scope.md`; use its supported
model, input, observable, unit, and validity contract when extending the adapter/UI.
`OrbitalModel` / `qm_model` implement the typed C++ adapter; `ComputeDispatch.cpp`
shares CPU/CUDA sampling and flow with Simulation and Engine. Adapter regressions
cover validation, screening, seeds, metadata, and atomic flow; dispatch regressions
inject CUDA failures to verify runtime fallback on CPU-only CI.
`atom_api` and `tools/serve.py` expose the local JSON API used by the TypeScript
UI in `web/`. The worker owns one active full cloud; browser responses contain
at most 20,000 preview samples. Keep physics in C++ and do not trust client
coordinates or silently change invalid requests. See `docs/local-ui.md` for
transport limits, stale-run behavior, and browser tests. Run `npm ci` and
`npm test` from `web/` after UI changes; CTest covers HTTP transport when Python
is available. CI requires the frontend job before releasing packages and bundles
compiled UI assets with the local server. `ApiSession` is shared with the browser
WebAssembly engine, which owns independent tab sessions and caps stationary
samples at 50,000. `SuperpositionModel` adds two distinct pure-Z orbitals (n <= 4,
at most 20,000 samples); its evolving density snapshots are resampled, not current
trajectories. Do not apply Slater screening to this shared-Hamiltonian model.
See `docs/superpositions.md`. Other model entries remain unavailable until validated.
Audio mappings must be labeled as sonification; isolated orbitals do not emit
literal audible sound in the current model.

Browser demo checks: install Emscripten 4.0.15, run `npm --prefix web run build:demo`
from the repository root, then `QM_TEST_WASM=1 npm exec -- playwright test` from
`web/`. CI runs both native and WebAssembly
projects. `.openai/hosting.json` binds the provisional Sites deployment; build
output in `dist` is generated and ignored. Keep its provisional status
and remaining hardware/data/model milestones visible.
