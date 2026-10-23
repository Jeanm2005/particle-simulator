# Renderer convergence and performance measurements

Measured 2026-10-07 on an Intel Core Ultra 9 285H (16 visible CPUs), using
llvmpipe (LLVM 21.1.8, 256 bits), surfaceless EGL and software OpenGL.
This is a small offscreen workload, not full-window frame rate or hardware GPU performance.

## Reproduce

```bash
cmake -S . -B build -DQM_ENABLE_CUDA=OFF -DBUILD_TESTING=ON
cmake --build build --parallel 2
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ./build/shader_tests --benchmark
```

`shader_tests` without an argument runs the same convergence checks without timing.
An unavailable EGL context returns 77. CI's existing required offscreen shader step
runs these image checks; timing has no pass/fail threshold.

## Sampling and checks

The old 512-step uniform grid produced a maximum RGB error of 0.0692 for 6s
against its 4096-step reference in the axial view. Even-sized images initially
hid this core defect; the final 49x33 image includes the central ray.

The new grid spaces samples uniformly in
`s = asinh((t + dot(camera, ray)) / (n / Zeff))` and transforms them back to ray
distance. This concentrates work near the ray's closest approach to the nucleus.
Each segment uses a midpoint density and its actual length in exponential opacity.
The default is now 256 segments, with an orbital-wide density scale.

Tests render 49x33 RGBA32F images for 1s, 2s, 2p (m=0), 3d (m=1),
4f (m=3), 6s, 7s, and 7f (m=0,3), from axial and tilted views. Cameras sit
at 5 n^2 / Zeff; the tilted view points from direction (0, 0.6, 0.8).
They compare 256/512/1024 segments with a 4096-segment reference, check finite
bounded colors, and verify Zeff=1 versus 79 at equivalent scaled geometry and
positive/negative m density symmetry. Existing checks retain ray misses and
cameras inside the volume.

Production images must have maximum RGB error <=0.005 and RMS error <=0.0003
against the reference. Errors are in linear RGB units on [0,1], excluding alpha.
Across these 18 views the measured largest production error was 0.003422.

## Timings

One run; each entry averages five draws synchronized with `glFinish`, after the
image render has warmed that state and step count. The timed region excludes
readback, CDF building, shader compilation, particle generation and display/vsync.
It includes submission and completion of the draw. Small images and synchronization
make these timings unsuitable for extrapolating to a 1280x720 window.

| State (n,l,m) | View | 256 steps ms | 512 steps ms | Max RGB error at 256 |
|---|---|---:|---:|---:|
| (1,0,0) | axial | 6.73 | 11.97 | 0.000276 |
| (1,0,0) | tilted | 6.01 | 11.80 | 0.000276 |
| (2,0,0) | axial | 6.36 | 11.82 | 0.000356 |
| (2,0,0) | tilted | 6.10 | 11.78 | 0.000357 |
| (2,1,0) | axial | 6.05 | 11.80 | 0.000362 |
| (2,1,0) | tilted | 6.18 | 12.32 | 0.000432 |
| (3,2,1) | axial | 6.13 | 12.04 | 0.000970 |
| (3,2,1) | tilted | 6.08 | 12.18 | 0.001101 |
| (4,3,3) | axial | 7.42 | 14.12 | 0.001117 |
| (4,3,3) | tilted | 7.18 | 15.15 | 0.001304 |
| (6,0,0) | axial | 7.69 | 16.77 | 0.003422 |
| (6,0,0) | tilted | 9.14 | 15.65 | 0.003421 |
| (7,0,0) | axial | 9.49 | 17.26 | 0.000793 |
| (7,0,0) | tilted | 9.42 | 16.48 | 0.000793 |
| (7,3,0) | axial | 8.71 | 17.56 | 0.000269 |
| (7,3,0) | tilted | 9.50 | 18.11 | 0.000825 |
| (7,3,3) | axial | 8.55 | 17.62 | 0.000317 |
| (7,3,3) | tilted | 8.52 | 18.02 | 0.000488 |

[Raw results](renderer-benchmark-20261007.txt) include all three step counts.
The 512-step timing column also uses the new stretched grid; it is not a timing
comparison against the old uniform implementation.

## Remaining validation

The 4096-step reference is a numerical rendering reference using the same density
shader, not an independent physical measurement. These samples do not establish
convergence for all states, camera positions, zoom levels or image resolutions.
Opacity and heatmap colors remain visualization mappings. Next checks should cover
interactive presentation and additional camera/quantum-state sweeps. The hardware
results below cover a full-window-sized shader workload; CUDA runtime parity and
speed results are recorded separately in [CUDA measurements](cuda-measurements.md).


## RTX 5080 Laptop GPU, 2026-10-23

The same convergence suite passed on `D3D12 (NVIDIA GeForce RTX 5080 Laptop GPU)`
through Mesa 26.0.8 on WSL2. Driver 617.42; Intel Core Ultra 9 285H host. Default
X11 EGL initially selected llvmpipe; explicitly using surfaceless D3D12 selected
the named NVIDIA adapter. Renderer identity is printed in the raw output and
must be checked before calling any run a hardware result.

`shader_tests --benchmark-full-window` first runs the existing 49x33 numerical
checks, then measures a **1280x720 RGBA32F offscreen** workload at the production
256 steps. This uses the existing shader, density scale, camera distance and two
view directions. It checks full-resolution pixels are finite/bounded, warms each
state/view with a checked readback plus three draws, then records 20 draws,
each synchronized with `glFinish`. Timings include CPU submission/driver overhead
and completion, excluding readback, compilation, density-scale construction,
window swap, compositor, UI, and vsync. This is full-window-sized shader throughput,
not an end-to-end interactive FPS benchmark or a full-resolution convergence proof.

One run; median milliseconds per draw:

| State (n,l,m) | Axial ms | Tilted ms |
|---|---:|---:|
| (1,0,0) | 1.667 | 1.724 |
| (2,0,0) | 1.713 | 1.757 |
| (2,1,0) | 1.846 | 1.730 |
| (3,2,1) | 1.998 | 2.287 |
| (4,3,3) | 2.088 | 2.039 |
| (6,0,0) | 2.651 | 2.677 |
| (7,0,0) | 2.707 | 3.181 |
| (7,3,0) | 3.838 | 3.644 |
| (7,3,3) | 3.080 | 2.783 |

The largest small-image production maximum RGB error was 0.003429, inside the
existing 0.005 gate; every production RMS error also passed. This verifies the
tested states/views on this backend only. Power/clocks were not locked; other
resolutions, camera positions, graphics drivers and sustained thermal conditions
remain unmeasured. [Raw results](renderer-benchmark-20261023.txt) include numerical
errors and full-resolution timing minima/maxima.

Reproduce on a WSL host with the D3D12 Mesa driver and NVIDIA adapter available:

```bash
cmake -S . -B build -DQM_ENABLE_CUDA=OFF -DBUILD_TESTING=ON
cmake --build build --parallel 4
EGL_PLATFORM=surfaceless GALLIUM_DRIVER=d3d12 \
  MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA \
  ./build/shader_tests --benchmark-full-window
```

Do not set `LIBGL_ALWAYS_SOFTWARE=1` for this hardware measurement. Native Linux
hosts should select their hardware EGL platform/driver instead. CTest retains
small software-renderer convergence checks, without timing thresholds or the
full-resolution benchmark. Interactive window presentation, point-cloud throughput,
and broader device coverage remain follow-up measurements.
