# CUDA hardware validation and dispatch timings

Measured 2026-10-23 on NVIDIA GeForce RTX 5080 Laptop GPU (16,303 MiB exposed),
Intel Core Ultra 9 285H (16 visible CPUs), WSL2 kernel 6.18.33.2, driver 617.42
(`nvidia-smi` utility 615.78.05), CUDA toolkit 13.4.92. Release build, C++ GCC
14.3 for C++ and GNU 15.2 as nvcc's detected default host compiler; architecture
120. CMake's generated CUDA command contains no host-compiler override (the
initial untyped `g++-14` cache request was not applied).
GPU was idle before the run; clocks/power were not locked. No speedup is promised
on other GPUs, power modes, operating systems or workloads.

## Numerical checks

`compute_tests --cuda` passed H 1s radial mean/isotropy and CPU/GPU statistical
comparison at 50k, 500k and 1M particles, coordinate flow parity for m = -1,0,1,
and representative 2p/3d/6s sampling at Zeff = 3.7. Heights are unchanged and
radius/coordinate errors remain inside the existing 1e-10/1e-9 tolerances.
CPU and GPU random streams differ: parity means these stated numerical checks,
not identical random samples or validation of every possible state.

## Repeated timings

The `--benchmark` option runs the parity checks, then benchmarks the same shared
`sampleCloud`/`advanceCloud` dispatch used by the application. Each backend/count
gets two warmups and seven measured repetitions. Reported values are medians of
wall-clock milliseconds. Both backends include their allocations, validation,
and output publication. CUDA includes device allocation, launches and transfers;
the returned cloud is host-resident. CDF construction and context initialization
are outside the repeated timings. Flow uses m = 1, dt = 0.01 on a sampled H 1s
cloud to exercise rotation; this is an integrator workload, not the physical
current of 1s (whose m is zero).

| Particles | CPU sample ms | GPU sample ms | Sampling speedup | CPU flow ms | GPU flow ms |
|---:|---:|---:|---:|---:|---:|
| 50,000 | 7.870 | 1.053 | 7.47× | 0.541 | 1.448 |
| 500,000 | 78.609 | 6.834 | 11.50× | 6.718 | 12.457 |
| 1,000,000 | 164.926 | 18.669 | 8.83× | 20.709 | 26.910 |

The GPU improves warmed sampling for these counts. Flow is slower with the
current host-return design. Persistent device storage/interop or choosing CPU
for small flow workloads should be evaluated separately before claiming faster
animation. Automatic dispatch behavior is unchanged by this measurement feature.
The first cold raw GPU sample took 133 ms at 50k, so short-lived sessions can
lose the warmed advantage. The preceding single-pass timings in the raw file
have a different boundary and should not be mixed with the dispatch table.

[Raw output](cuda-benchmark-20261023.txt) includes min/max values for all repeated
timings, the cold sample, and the successful parity marker. No performance
threshold is added to CI; CI still verifies compilation and CPU runtime fallback.

## Reproduce

```bash
cmake -S . -B build-cuda -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_CUDA_ARCHITECTURES=120 -DQM_ENABLE_CUDA=ON
cmake --build build-cuda --parallel 4
./build-cuda/compute_tests --cuda --benchmark
ctest --test-dir build-cuda --output-on-failure
```

Use an architecture/compiler supported by the installed toolkit and target GPU.
The runner returns 77 when `--cuda` or `--benchmark` is requested without a usable
CUDA device. On sandboxed hosts, GPU execution needs device access outside the
sandbox; a successful `nvidia-smi` probe alone is insufficient evidence of parity.
