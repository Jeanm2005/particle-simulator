# Two-orbital interference

`orbital-superposition` evolves the coherent density of two distinct orthonormal
hydrogenic orbitals in the same fixed pure-Z Coulomb Hamiltonian. It is available
in the TypeScript UI, native JSON worker, and browser WebAssembly engine. The
native console and OpenGL viewer retain their stationary-orbital interface.

## Physics and interpretation

For second-state probability weight w and initial relative phase φ,

```text
ψ(r,t) = √(1-w) ψa(r) exp(-i Ea t) + √w exp(iφ) ψb(r) exp(-i Eb t)
En = -Z² / (2n²)                    (Hartree)
ρ(r,t) = |ψ(r,t)|²                  (a0⁻³)
<E> = (1-w) Ea + w Eb               (converted to eV in results)
```

Time is in atomic units, so ħ = 1 in the phase evolution. Complex spherical
harmonics use Y as the polar axis, φcoordinate = atan2(z,x), and the
Condon–Shortley phase. Negative m uses Y_l^-m = (-1)^m conjugate(Y_l^m).
Orthogonality and weights summing to one ensure normalized density. Distinct
energies produce interference at angular frequency |Ea-Eb|; equal-energy basis
states have stationary density even when their coefficients have a relative phase.

For equal H 1s + 2p(m=0), the beat period is 16π/3 atomic units. Mean radius is
3.25 a0, energy expectation is -0.3125 Hartree, and mean Y is
[256/(243√2)] cos(3t/8 - φ) a0. The dipole changes sign at half a beat and
vanishes at a quarter beat. Energy is an expectation value rather than one
orbital eigenenergy.

At each model time the CPU sampler proposes positions from the weighted mixture
(1-w)|ψa|² + w|ψb|² using existing radial/angular CDFs and accepts against the
coherent density. Cauchy–Schwarz bounds coherent density by twice this mixture.
The proposal retains the existing finite CDF/domain approximation; this is not
an exact continuous numerical sampler. The accepted cloud is a density snapshot.
Advancing resamples with the same seed; points are **not tracked electron or
probability-current trajectories**. Stationary-orbital current flow retains its
existing radius/Y invariants separately.

## Input contract

Use the version-1 orbital request with model `orbital-superposition`, first state
in `state`, and this additional required field:

```json
"superposition": {
  "state": {"n": 2, "l": 1, "m": 0},
  "weight": 0.5,
  "phase": 0
}
```

Both states require n in [1,4], l in [0,min(3,n-1)], m in [-l,l], and must differ.
Use `screening: "pure-z"`; separately screened orbitals do not define the shared
orthogonal basis assumed here. Weight is finite in [0,1]; phase is finite with
absolute value at most 1,000,000 radians. At most 20,000 samples are accepted.
Cumulative time satisfies |t|Z² ≤ 1,000,000 atomic units to bound phase precision.
CPU is used even under `auto`, with explicit execution metadata. Invalid inputs
leave the prior session intact.

Results include both states, coefficients, model time, energy expectation,
sampled mean radius and mean Y, plus the common cloud/execution metadata. Units,
nonrelativistic one-electron validity limits, and metadata caveats from the
[model contract](model-scope.md) apply. No external perturbations, transitions,
radiation, or spin are modeled. This feature does not emit audio.

## Verification

`superposition_regressions` checks analytic cross terms and phases, beat return,
degenerate states, negative-m convention, independent numerical volume
normalization at three times, seed repeatability, pure-state endpoints, sampled
mean radius/dipole, conserved energy, invalid inputs, and transactional updates.
HTTP and browser tests check density evolution and errors through both engines.
These checks cover representative combinations, not every possible state pair;
extended basis validation remains on the roadmap.

The time-evolution convention follows the energy-eigenstate expansion in
[MIT OpenCourseWare, Chapter 6](https://ocw.mit.edu/courses/22-02-introduction-to-applied-nuclear-physics-spring-2012/b5106a499ae03e36b5a2e002355668f9_MIT22_02S12_lec_ch6.pdf).
