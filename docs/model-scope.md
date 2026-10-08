# Supported models and product contract

Status: the scope milestone is complete. The contract below is a design for the
next implementation stage, not an existing JSON API or TypeScript application.
The current entry points remain the C++ console and OpenGL viewer.

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

The first adapter will use structured quantum numbers instead of parsing display
labels. Its initial supported range matches the console: n = 1 through 7,
l = 0 through min(3, n - 1), and integer m with -l <= m <= l. The core sampler
can accept other values, but that does not expand this product contract.

| Input | Validation and meaning |
|-------|------------------------|
| `model` | Exactly `hydrogenic-orbital` for the initial adapter |
| `element.atomicNumber` | Positive integer representable by the C++ atomic-number type; authoritative identity |
| `state.n`, `state.l`, `state.m` | Whole integers satisfying the supported ranges above |
| `screening` | `pure-z` or `slater-neutral`; required explicitly |
| `sampleCount` | Positive integer; the adapter must publish and enforce a resource limit before allocating |
| `seed` | Unsigned 32-bit integer for repeatable sampling within a given backend/version |
| `backend` | `auto` or `cpu`; `auto` permits CUDA with CPU fallback |

Element symbols, masses, and electron configurations come from the backend's
element database. The client must not supply its own configuration or effective
charge. Unknown positive Z can use pure Z, as in the current console; the response
must identify missing metadata. Slater mode requires an expanded configuration
whose electron count matches Z and an occupied selected Slater group. A failure
must explain that pure Z is available without silently changing the request.

Example proposed request (not currently accepted by `atom_sim`):

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

The current `Simulation` always initializes its CPU generator with 42; exposing
the seed and backend choice is work for the adapter stage. CPU and CUDA random
streams differ. Reproducibility must not promise matching clouds across backends.

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

The initial numerical result consists of the cloud, energy, and sampled mean
radius already produced by `SimulationResult`. Numerical density queries and
current-update commands need explicit adapter operations before being advertised.
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

Keep equations and validation in the C++ physics layer. A future model adapter
owns validated requests/results and delegates sampling/current operations to the
compute backend. Rendering consumes results; neither OpenGL nor UI state belongs
in the model interface. Transport choice (local process, native bridge, or server)
is a separate decision for the next stage.

The adapter should expose model capabilities and limits before execution. It
must reject unknown contract versions, model IDs, fields, noninteger inputs,
nonfinite values, invalid states, and requests above its declared resource limit.
Validation precedes sampler construction and allocation. Numerical failures must
return an error rather than nonfinite results or a partial cloud.

Proposed stable error codes are `unsupported-version`, `unsupported-model`,
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
