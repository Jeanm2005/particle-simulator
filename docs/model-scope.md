# Supported models and product contract

Status: the scope milestone and typed C++ orbital adapter are implemented.
`qm_model` exposes `OrbitalModel` in `include/OrbitalModel.hpp`. The local JSON
transport and TypeScript orbital UI now use this adapter; see
[the local UI guide](local-ui.md). C++ console and OpenGL viewer entry points
remain available.

## Model catalog

A user selects a physics model before entering a particle or state. A particle
name alone is insufficient to choose equations, interactions, or observables.

| Model ID | System and inputs | Outputs | Availability and limits |
|----------|-------------------|---------|-------------------------|
| `hydrogenic-orbital` | Electron orbital in a fixed central Coulomb model; element Z, n/l/m, pure Z or Slater screening | Probability cloud, orbital density visualization, energy, sampled mean radius, probability-current animation | Implemented in C++; stationary, nonrelativistic model; screening is approximate |
| `orbital-superposition` | Coherent orbital coefficients, phases, compatible basis, time | Time-dependent density and interference | Planned; needs normalization, evolution, and current validation |
| `lattice-dynamics` | Interacting lattice, masses, force model, boundaries, initial displacement/velocity | Displacements and mode observables | Exploratory; no lattice implementation exists |

Only `hydrogenic-orbital` may be offered as runnable in the first UI. Other
particle categories, including free elementary particles and arbitrary composite
systems, remain unsupported until they have their own equations and validation.
Do not turn unsupported particle input into an orbital cloud.

## Orbital input contract

The adapter uses structured quantum numbers instead of parsing display labels.
Its supported range matches the console: n = 1 through 7,
l = 0 through min(3, n - 1), and integer m with -l <= m <= l. The core sampler
can accept other values, but that does not expand this product contract.

| Input | Validation and meaning |
|-------|------------------------|
| `model` | Exactly `hydrogenic-orbital` for the initial adapter |
| `element.atomicNumber` | Positive integer representable by the C++ atomic-number type; authoritative identity |
| `state.n`, `state.l`, `state.m` | Whole integers satisfying the supported ranges above |
| `screening` | `pure-z` or `slater-neutral`; required explicitly |
| `sampleCount` | Positive integer, at most 1,000,000; validated before sampling allocation |
| `seed` | Unsigned 32-bit integer for repeatable sampling within a given backend/version |
| `backend` | `auto` or `cpu`; `auto` permits CUDA with CPU fallback |

Element symbols, masses, and electron configurations come from the backend's
element database. The client must not supply its own configuration or effective
charge. Unknown positive Z can use pure Z, as in the current console; the response
must identify missing metadata. Slater mode requires an expanded configuration
whose electron count matches Z and an occupied selected Slater group. A failure
must explain that pure Z is available without silently changing the request.

Example `POST /api/sample` request (the interactive `atom_sim` does not parse JSON):

```json
{
  "contractVersion": 1,
  "model": "hydrogenic-orbital",
  "element": { "atomicNumber": 1 },
  "state": { "n": 1, "l": 0, "m": 0 },
  "screening": "pure-z",
  "sampleCount": 50000,
  "seed": 42,
  "backend": "cpu"
}
```

`Simulation` accepts a seed and backend preference; its existing callers retain
seed 42 and automatic CUDA/CPU selection. Adapter calls construct a new simulation
for each request. CPU and CUDA random streams differ. Reproducibility must not
promise matching clouds across backends, compiler/standard-library versions, or
automatic execution that changes backend. An attempted CUDA sample consumes a
generator value before falling back to CPU after a runtime failure.

## Results, units, and interpretation

Every result must carry the contract/model version, validated state, element
identity, screening mode, resolved effective charge, requested and actual sample
counts, seed, backend used, and any fallback reason. Backend selection is an
execution detail and must not change the selected physics model.

| Quantity | Unit / representation | Interpretation |
|----------|-----------------------|----------------|
| Cloud | Cartesian x/y/z in a0; Y is the polar axis | Samples of orbital probability density; not individual electron trajectories |
| Energy | eV | Current hydrogenic energy expression using the resolved charge; approximate under screening |
| Mean radius | a0 | Sample mean, with sampling error; not an exact expectation value |
| Density | a0^-3, if a numerical density API is added | Normalized model density; shader opacity is a separate visual mapping |
| Current animation | Atomic-unit time, rotation about Y | Probability-current flow of the selected stationary state |

The numerical result consists of the cloud, energy, and sampled mean radius
produced by `SimulationResult`. `OrbitalModel::advance` updates a result's cloud
in atomic-unit time and returns execution metadata for that operation separately
from the sampling metadata. Numerical density queries are not exposed yet.
Rendered brightness, point size, color, opacity, camera pose, and playback speed
are display controls. They must not be returned as physical observables.

Current animation preserves each sample's radius and Y. Advancing samples does
not change the state's energy or stationary density. Atomic-unit time must be
distinguished from elapsed UI seconds and its playback multiplier.

## Validity and required UI disclosures

The orbital panel must identify the modeled quantity as electron probability
density and show the selected model and screening approximation beside results.
Pure-Z wavefunctions describe the existing nonrelativistic one-electron model.
Selecting a neutral multi-electron element does not make that model an exact
many-electron solution. Slater screening does not simulate electron interactions
or excited-state configuration rearrangement.

The implemented model does not include spin dynamics, relativistic corrections,
external fields, collisions, radiation, nuclear motion, or time-dependent state
superpositions. Large Z does not remove these limitations. Element metadata,
especially the Z = 119 entry, still requires a provenance review; selection in
the database is not evidence of experimental verification.

Sampling uses finite tabulated CDFs and a bounded radial domain. The ray marcher
uses a finite-step visual approximation. The UI must keep quantitative sampled
statistics separate from visual opacity and link to the existing renderer
accuracy notes. Any later audio feature must be labeled sonification, with its
mapping documented; it is not sound emitted by an isolated orbital.

## Backend boundary and errors

Keep equations and validation in the C++ physics layer. The model adapter
owns validated requests/results and delegates sampling/current operations to the
compute backend. Rendering consumes results; neither OpenGL nor UI state belongs
in the model interface. Transport choice (local process, native bridge, or server)
is a separate decision for the next stage.

`OrbitalModel::capabilities()` exposes versions, orbital limits, sample limits,
and units before execution. The typed adapter rejects unknown contract versions,
model IDs, enum values, invalid states, nonfinite current inputs, and requests
above its declared resource limit. Integers use C++ integer types; the JSON
transport parser rejects unknown fields, fractional inputs, and overflow
before conversion to these types.
Validation precedes sampler construction and allocation. Numerical failures must
return an error rather than nonfinite results or a partial cloud.

Stable `ModelError` codes are `unsupported-version`, `unsupported-model`,
`invalid-input`, `screening-unavailable`, `resource-limit`, and
`numerical-failure`. Each error carries a readable message and, when applicable,
the input field. CUDA unavailability is a reported fallback under `auto`, not a
physics validation failure. Requests must not be retried with different physics.

## Acceptance gates for the next stage

1. Add a graphics-independent orbital model adapter implementing the validated
   request/result contract. Preserve the existing console and viewer behavior.
2. Route execution through shared CPU/CUDA sampling and flow dispatch; record the
   backend actually used and retain runtime CPU fallback.
3. Test H 1s pure Z, Au 6s Slater (Zeff = 3.7), invalid n/l/m, unknown model,
   unoccupied screening group, missing configuration, resource limits, and seed
   repeatability on CPU. Reuse existing physics and flow regression coverage.
4. Choose transport and connect a TypeScript UI to the same validation contract.
   Show units, approximations, unsupported models, and errors; include a console
   build check so UI integration never requires OpenGL for physics execution.

New model IDs require documented inputs, interactions, boundary conditions,
observables, units, validity limits, and independent numerical checks before
being enabled. NVIDIA parity/performance and hardware-renderer measurements
remain separate open validation tasks for the existing implementation.

Gates 1–3 are implemented and covered by `model_regressions` and
`dispatch_regressions`. The latter uses a failing CUDA provider to exercise
runtime fallback on CPU-only machines. Gate 4 is implemented by the local HTTP
server, native worker, and TypeScript UI, with HTTP integration and browser tests.

### C++ adapter use

```cpp
#include "OrbitalModel.hpp"

qm::ElementDatabase elements;
qm::OrbitalModel model(elements); // owns a snapshot of the database
qm::OrbitalRequest request;
request.backend = qm::BackendPreference::Cpu;
request.seed = 42;
auto result = model.run(request); // H 1s, 50,000 samples
auto execution = model.advance(result, 0.1); // dt in atomic units
```

Link against the CMake target `qm_model`; it has no OpenGL dependency.
`OrbitalResult::request` retains all validated inputs, `element` and
`metadataAvailable` identify the database lookup, and `observables.execution`
records the sampling backend and fallback reason. `actualSampleCount` reports
the produced size; units and model version are available in capabilities.
Results are mutable C++ data, not authenticated state handles; callers must keep
their request and cloud consistent. Updates validate state/count consistency and
coordinates before publishing a new cloud. They preserve sampling statistics,
radius, and Y; a failed update leaves the cloud unchanged.
