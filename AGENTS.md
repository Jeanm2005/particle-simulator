
---

### `AGENTS.md`

```markdown
# AGENTS.md — collaboration notes for humans and AI agents

This file orients **developers** and **coding agents** working on the Quantum Atom Simulator.

## Project location

- Canonical working tree (this machine / user): `~/havefun/atoms` or the repo root after clone  
- Sandbox / CI style path may appear as `/home/workdir/artifacts/atoms`  
- Always run the binary from the **repo root** so `data/elements.json` resolves

## What this project is

C++17 quantum orbital visualiser:

- Physics: hydrogenic \(R_{nl}\), \(Y_l^m\), CDF sampling, \(Z\) / Slater \(Z_\text{eff}\)
- Rendering: GLFW + OpenGL 3.3 (point cloud + fragment-shader ray march)
- Data: `data/elements.json`
- Reference inspiration: [kavan010/Atoms](https://github.com/kavan010/Atoms)

## Hard requirements vs optional

| Item | Priority |
|------|----------|
| Correct Schrödinger sampling + multi-element \(Z\) | Required |
| OpenGL point cloud + camera + live \(n,l,m\) | Required |
| Probability-current animation | Required (done; keep working) |
| GPU raytracer path | Required (done; improve over time) |
| **CUDA acceleration** | **Required next milestone** (not optional) |
| Agentic AI workflow (this file, PR bots, etc.) | Optional |
| 2D Bohr model | Out of scope (explicitly skipped) |

## CUDA milestone (required)

Implement under `cuda/`:

1. **Sampling kernels** — parallel inverse-CDF or rejection sampling for large \(N\)  
2. **Flow update kernel** — `probabilityCurrentVelocity` on GPU for all particles  
3. **CMake** — `option(QM_ENABLE_CUDA ...)` + `enable_language(CUDA)` when toolkit present  
4. **Fallback** — CPU path must still build and run without CUDA  

Do not remove the CPU OpenGL path when adding CUDA.

## Architecture rules

- Physics stays in `QuantumMath`, `RadialSampler`, `Simulation` — no OpenGL includes there  
- Rendering stays in `Engine` / `Camera`  
- Prefer C++17, CMake 3.16+, no single-file monolith  
- Do not hardcode \(\pi\); use `qm::PI` from `Constants.hpp`  
- Element tables live in JSON, not hardcoded arrays (fallback table only for offline emergency)

## Build / test checklist for agents

```bash
cmake -B build -S .
cmake --build build
./build/atom_sim --console   # must work without display
# optional if GL works:
./build/atom_sim --visual

Console regression: input H, 1s, p → finite $\langle r \rangle$, writes H_1s_cloud.xyz.
Common environment issues

WSL + GLEW failed: software GL (LIBGL_ALWAYS_SOFTWARE=1) or native Windows build
Old banner / no window: stale binary — rm -rf build && cmake -B build && cmake --build build
m must be integer with $|m| \le l$

Optional agentic AI workflow
These are optional process helpers, not product features:

Issue-driven tasks — one milestone per issue (e.g. “CUDA particle update kernel”)
Agent commits — small diffs; never rewrite unrelated modules in the same PR
AGENTS.md + README — keep roadmap and constraints in sync when behaviour changes
CI (future) — Linux build job: configure without CUDA, run --console smoke test
Code review prompts — check physics units (a0), polar axis convention (Y-up), and that QM_HAS_OPENGL guards remain correct

Agents should prefer reading this file and README.md before large refactors.

File ownership (rough)

































AreaPrimary filesWavefunctions / currentQuantumMath.*SamplingRadialSampler.*Console I/O + XYZSimulation.*, main.cppOpenGL / flow / raytraceEngine.*, Camera.hppElementsElement.*, data/elements.jsonCUDAcuda/*, CMake CUDA option
Reference mapping (kavan010/Atoms)





























Their ideaOur locationLaguerre recurrence samplingQuantumMath::associatedLaguerre, CDF in RadialSamplerReal-time point cloudEngine point pathRaytracerEngine::createRaytraceShaders / drawRaytraceProbability flowprobabilityCurrentVelocity + Engine::updateParticlesHydrogen-onlyExtended to any $Z$ via element DB + Slater