# Quantum Atom Simulator

Interactive **3D** quantum orbital visualiser in modern C++.

Exact hydrogenic wavefunctions from the Schrödinger equation, scaled by nuclear
charge \(Z\) or Slater \(Z_\text{eff}\), so the same code works for **any element**.

## What this is

A computational-physics / visualisation project that:

1. Samples electron probability density \(|\psi_{nlm}|^2\) from exact hydrogenic orbitals
2. Renders a live OpenGL **point cloud** of those samples
3. Optionally animates particles along the quantum **probability current**
4. Optionally ray-marches a volumetric density field on the GPU (fragment shader)
5. Loads element data from `data/elements.json` (Z, symbol, config, mass)

Console-only mode remains available when OpenGL is unavailable (writes `.xyz` clouds).

## Reference

Physics sampling and visualisation ideas are informed by:

- **[kavan010/Atoms](https://github.com/kavan010/Atoms)** — Hydrogen Quantum Orbital Visualizer  
  (real-time OpenGL point clouds, raytracer, Laguerre/Legendre sampling)

This project extends that direction with:

- Multi-element support via \(Z\) / \(Z_\text{eff}\)
- Modular CMake layout and clean separation of physics vs rendering
- Dual CDF sampling (radial + angular)
- Probability-current flow integrated into the live viewer
- Planned **CUDA** acceleration (required next milestone, not optional)

## Features

| Feature | Status |
|---------|--------|
| OpenGL point cloud of \(\|\psi\|^2\) | Done |
| Orbit / zoom camera | Done |
| Live change of \(n, l, m\) | Done |
| Any element via JSON | Done |
| Dual CDF sampling | Done |
| Probability-current flow (key **P**) | Done |
| GPU volumetric raytracer (key **T**) | Done |
| **CUDA** parallel sampling & updates | **Planned (required)** |
| Agentic AI workflow helpers | Optional |

## Controls (visual mode)

| Input | Action |
|-------|--------|
| Mouse drag | Orbit |
| Scroll | Zoom |
| **N** / **B** | \(n\) ↑ / ↓ |
| **L** / **K** | \(l\) ↑ / ↓ |
| **M** / **J** | \(m\) ↑ / ↓ |
| **P** | Toggle probability-current flow |
| **T** | Toggle POINTS ↔ RAYTRACE |
| **,** / **.** | Flow speed ↓ / ↑ |
| **+** / **-** | Particle count |
| **[** / **]** | Point size |
| **R** | Resample |
| **Esc** | Quit |

## Physics (brief)

- Radial \(R_{nl}(r)\): associated Laguerre recurrence + `tgamma` normalisation  
- Angular \(Y_l^m\): associated Legendre  
- Sampling: CDF inversion for \(r\) and \(\theta\); \(\phi\) uniform  
- Probability current (atomic units, polar axis = Y):

\[
\mathbf{v} = \frac{m}{r\sin\theta}\,\hat\phi
\]

- Energy: \(E_n = -13.6\,Z^2/n^2\) eV (exact for one-electron ions)

Console input requires whole integers for atomic numbers and m, exact orbital
labels (1s through 7f, with l < n), and explicit p/s and v/c choices. Invalid
input exits with an error instead of silently substituting another orbital.
Both exported clouds and the viewer use Y as the polar axis. Current animation
rotates samples about Y, preserving their radius and height.

## Screening and validation

Slater screening follows [the grouped shielding rules](https://lampz.tugraz.at/~hadley/ss1/molecules/atoms/slater.php)
and uses the expanded neutral electron configuration in the element
JSON. The selected ns/np, nd, or nf group must be occupied. For unoccupied groups
or custom elements without a configuration, select pure Z instead. Screening is
an approximation; it does not model excited-state electron rearrangements.
Invalid sampler parameters and nonfinite distributions are rejected with errors.

Run the screening and sampling regressions with `ctest --test-dir build --output-on-failure`.

## Build

```bash
# Debian / Ubuntu / WSL
sudo apt install build-essential cmake \
  libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev

mkdir build && cd build
cmake ..
cmake --build .

Run
Bash# from project root so data/elements.json is found
./build/atom_sim --visual
./build/atom_sim --console

WSL / no GPU
Bashexport LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
./build/atom_sim --visual
If GLEW still fails, use --console and open the .xyz file in Ovito or VMD.

Layout
textCopyCopiedatoms/
├── CMakeLists.txt
├── README.md
├── AGENTS.md              # notes for human + AI collaborators
├── data/elements.json     # Z = 1 … 119
├── include/               # public headers
├── src/                   # implementations
└── cuda/                  # CUDA kernels (in progress)
Roadmap
Required next: CUDA

Parallel radial/angular CDF construction and sampling on GPU
Parallel particle position updates under probability current
Optional dense volume evaluation for the raytracer
CMake flag -DQM_ENABLE_CUDA=ON and graceful CPU fallback

See cuda/README_CUDA.md and AGENTS.md.
Later

Multi-orbital display
Better raytracer adaptive stepping
Native Windows MSVC build instructions

Accuracy

Exact for one-electron ions (H, He⁺, Li²⁺, …)
Approximate for multi-electron atoms (screening via $Z_\text{eff}$)
For quantitative chemistry use Hartree–Fock / DFT packages