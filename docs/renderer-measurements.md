# Renderer convergence and software baseline

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
full-window workloads on hardware GPUs and additional camera/quantum-state sweeps.
CUDA runtime parity and speed measurements still need usable NVIDIA hardware.
