# Provisional recruiter demo

This deployment presents a working stage of the project for internship review.
It is labeled **provisional / in development**, with a quick start for H 1s + 2p
interference and a source/roadmap link. It is not a completion claim.

The static demo runs the same C++ `ApiSession`, `OrbitalModel`, and
`SuperpositionModel` compiled to WebAssembly. A Web Worker performs CPU sampling
and evolution; TypeScript renders the resulting cloud with WebGL. There is no
public Python server, hosted CUDA execution, or JavaScript replacement for the
physics. Each visitor tab owns an independent model session.

Stationary sampling is capped at 50,000 points, superpositions at 20,000, and
preview rendering at 20,000. The C++ native API retains its 1,000,000 stationary
sample limit. Browser playback advances once per computed update rather than
promising real-time physical speed. A failed worker or 35-second timeout requires
resampling. WebGL-capable modern browsers are needed for the interactive cloud;
renderer failures retain numerical output.

## Reproduce the static build

Install Node.js 22+ and [Emscripten 4.0.15](https://emscripten.org/docs/getting_started/downloads.html).
With em++ on PATH, from the repository root:

```bash
npm --prefix web ci
npm --prefix web run build:demo
python3 -m http.server 8001 --bind 127.0.0.1 --directory dist
```

Alternatively set `EMXX` to the absolute em++ executable for the build command.
Open `http://127.0.0.1:8001`. Generated assets in `dist` include compiled
TypeScript, the C++ Wasm module, and preloaded element data; all are required.
The generated directory and compiler scratch files are ignored by Git.
[Embind](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/embind.html)
exposes the session's string protocol to the Web Worker.

Build native `atom_api` first, then test both engines:

```bash
cd web
QM_TEST_WASM=1 npm exec -- playwright test
```

Tests serve the static demo at loopback port 8766 and the native bridge at 8765.
They compare browser/native screening results, check rendered pixels, interference,
current invariants, separate visitor sessions, invalid states, playback and mobile
layout. CI builds pinned Emscripten and runs these checks.

`.openai/hosting.json` records the single Sites project and static output path.
Publishing packages generated output from the committed source and keeps the
provisional audience/status. The live URL is supplied after successful deployment.

## Remaining work

NVIDIA runtime parity/performance, hardware-renderer measurements, scientific
data provenance, broader particle models, sonification/WAV export, and lattice
physics remain open. See the [roadmap](../README.md#product-scope-and-future-milestones)
and [superposition limits](superpositions.md). No browser GPU acceleration or
literal sound emission is claimed.
