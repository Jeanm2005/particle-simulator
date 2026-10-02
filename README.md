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

From the repository root, using CMake 3.16+ and a C++17 compiler:

```bash
# Debian / Ubuntu / WSL dependencies for the viewer
sudo apt install build-essential cmake \
  libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev

cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite includes offscreen shader regressions when EGL is available;
those checks are skipped if an offscreen context cannot be created.

When graphics dependencies are missing, CMake builds console mode. To explicitly
check that fallback:

```bash
cmake -S . -B build-console -DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=ON
cmake --build build-console
ctest --test-dir build-console --output-on-failure
```

## Run

```bash
./build/atom_sim --visual
./build/atom_sim --console
```

Both modes prompt for the element, orbital, and screening choice. Nonspherical
orbitals also prompt for integer m. Running without an option prompts for the mode
when graphics support is compiled. `--help` lists the command-line options.
XYZ output is written to the current directory in atomic units (a0). Failure to
open or finish writing the file returns a nonzero exit status. Viewer startup
also returns a nonzero exit status if context creation, shader compilation, or
shader linking fails.

### Install and element data

```bash
cmake --install build --prefix "$HOME/.local"
"$HOME/.local/bin/atom_sim" --console
```

The database searches these locations in order:

1. `QM_DATA_DIR/elements.json`, when the environment variable is set.
2. `data/elements.json` relative to the current directory.
3. The installed data directory relative to the executable (also after relocating
   an installation or launching through PATH).
4. The installation data directory configured by CMake.
5. The source tree's data directory, for development builds.

If none loads, the program prints a warning and uses its six-element emergency
table. An explicit path supplied to `ElementDatabase` bypasses automatic discovery.

### WSL / software graphics

```bash
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
./build/atom_sim --visual
```

If graphics initialization still fails, use console mode and import the XYZ file
into a viewer that supports particle clouds. Coordinates are in a0; convert to
angstroms when needed using the factor in `Constants.hpp`.

## Layout

```text
atoms/
├── CMakeLists.txt
├── README.md
├── AGENTS.md
├── cmake/                # generated data-path configuration template
├── data/elements.json    # entries Z = 1 … 119
├── include/              # public headers
├── src/                  # implementations
└── tests/                # physics, input, and installation regressions
```

The dataset includes an entry for Z = 119. Its scientific metadata and the other
element records still need a provenance review.

## Roadmap

- [x] Fix screening, validate finite distributions, and cover the crash with tests.
- [x] Fix C++17 portability, input parsing, coordinates, and probability flow.
- [x] Repair documentation, installed data discovery, and export/shader errors.
- [ ] Implement CUDA sampling and flow kernels with CPU fallback and parity tests.
- [ ] Improve ray-march bounds, stepping, opacity, and camera consistency; benchmark.

CUDA is the required next milestone. It has not been implemented: there is no
`cuda/` directory or `QM_ENABLE_CUDA` option yet. The milestone will add parallel
sampling and particle updates while preserving the CPU OpenGL path. Optional
GPU volume evaluation can follow.

Later work includes multi-orbital display, scientific data provenance, automated
CI, and native Windows build instructions.

## Accuracy

Hydrogenic wavefunctions are exact for the nonrelativistic one-electron model.
Multi-electron screening is approximate. Ray-march rendering is currently a
visual approximation with fixed stepping and normalization limitations. For
quantitative chemistry, use a suitable Hartree–Fock or DFT package.
