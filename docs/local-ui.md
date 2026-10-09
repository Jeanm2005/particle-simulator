# Local orbital lab

The TypeScript UI uses a same-origin, loopback-only Python HTTP server and a
persistent `atom_api` C++ worker. Sampling and current updates execute through
`OrbitalModel`; no wavefunctions or current equations are implemented in browser
code. This guide covers the local Linux development/desktop interface; the
[provisional public demo](browser-demo.md) uses the same C++ session compiled
to WebAssembly, with no Python server.

## Build and run

From the repository root, with Python 3.10+, Node.js 22+ and the C++ build tools:

```bash
cmake -S . -B build
cmake --build build --parallel 2
npm --prefix web ci
npm --prefix web run build
python3 tools/serve.py --binary build/atom_api
```

Open `http://127.0.0.1:8000`. Use `--port` for a different local port. The server
binds to 127.0.0.1; it does not open a browser automatically. Ctrl+C stops the
server and its worker. Physics and transport work with OpenGL disabled:

```bash
cmake -S . -B build-console -DQM_ENABLE_CUDA=OFF -DCMAKE_DISABLE_FIND_PACKAGE_OpenGL=ON
cmake --build build-console --parallel 2
python3 tools/serve.py --binary build-console/atom_api
```

The browser uses WebGL for its point cloud. If browser graphics are unavailable,
the page retains numerical results and displays a renderer message. The native
OpenGL point/ray-march viewer remains available separately; the browser currently
renders points only.

Tested release packages include the compiled UI and server. From an extracted
Linux package, run (Python 3.10+ required, Node not required):

```bash
python3 share/atom_sim/tools/serve.py --binary bin/atom_api
```

A plain `cmake --install` installs native binaries and data, but does not compile
or install UI assets; the release workflow explicitly bundles the built UI.
Native Windows server support remains future work. Keep this server on loopback;
the public demo has a separate static build.

## Using the interface

Choose a database element or enter a custom atomic number, then select n/l/m,
charge mode, sample count, seed, and compute preference. The interface enforces
whole integers and the declared limits; C++ independently validates model
semantics. Invalid Slater choices show an error and require an explicit change
to pure Z. A failed request retains previous results; edited controls are labeled
as pending and cannot advance the previous run until resampled.

Drag the canvas to orbit and scroll to zoom. Keyboard users can focus the canvas
and use arrow keys to rotate, plus/minus to zoom, and Reset view to restore the
camera. Coordinates use a0 with Y as the polar axis.

Step and Play send current updates to C++, using the entered delta time in atomic
units. Playback schedules the next step after the previous backend response;
it does not promise real-time simulation speed. Pause and page visibility changes
stop further scheduled steps. One step already in flight can still complete.
For a stationary m = 0 orbital, samples stay fixed while model time advances.

Choose Two-orbital superposition, or use the 1s + 2p quick start, to evolve
interference. Both states share pure Z; screening is disabled. Enter second-state
weight in [0,1] and relative phase in radians. Each step resamples the density at
the new time; these points do not track trajectories. See
[the superposition contract](superpositions.md) for limits and analytic checks.

The full cloud stays in the worker. Responses contain at most 20,000 preview
points, selected at fixed evenly spaced sample indices, plus statistics and
counts for the full requested cloud (up to 1,000,000 samples). The preview shows
its displayed/sample counts. Current updates preserve preview sample identity.
Camera scale is fitted when sampling a new run and retained across updates; point color uses scaled
radius and brightness is a display mapping. Neither represents a measured
observable. Mean radius and mean Y are computed from every sampled point.

## JSON HTTP contract

All responses are JSON except UI static assets. No CORS access is enabled;
requests must use this server's local Host and Origin. Bodies must have
Content-Type `application/json` and be at most 8192 bytes. Unknown/missing fields,
duplicate fields, fractional integer inputs, booleans in numeric fields, and
out-of-range integer conversions are rejected before invoking C++.

- `GET /api/catalog` returns model/version, n/l/count limits, preview limit, units,
  and the backend's loaded element records. Emergency fallback records are used
  only if element-data discovery fails, as in native modes.
- `POST /api/sample` accepts the version-1 orbital request from
  [the model contract](model-scope.md). All fields are required. It returns the
  validated request, model version, element identity and metadata availability,
  effective charge, energy in eV, mean radius and mean Y in a0, full/preview counts, preview
  points, sampling/operation backend metadata, units, and an opaque `runId`.
- `POST /api/advance` accepts exactly `{"runId":"...","dt":0.1}`. Delta time
  must be finite and is in atomic units. It updates the worker's full cloud and
  returns the same result shape with cumulative `timeAtomicUnits` and operation
  metadata. Clients cannot supply coordinates or substitute an effective charge.

Each server owns **one active run**. A successful sample replaces it; old handles
return HTTP 409 with `invalid-input` on `runId`. This also applies to multiple
browser tabs. Failed validation does not replace the active run. The server
executes one native operation at a time; concurrent operations return HTTP 409
`backend-busy` rather than entering an unbounded queue.

Errors use `{"error":{"code":"...","field":"...","message":"..."}}`.
Native model validation generally returns 422, framing errors 400, resource
limits 413, stale handles or busy execution 409, and worker availability failures
503 `backend-unavailable`. A native operation has a 30-second timeout and an
8-MiB response limit. Worker failures terminate the process and invalidate its
handle; a later request starts a new worker and a new sample is required. These
transport errors are separate from model error codes. The browser uses a
35-second request timeout.

The native worker reads one compact text command per line on stdin and emits
one JSON response per line on stdout. Only the server constructs commands; it
passes validated numeric types and escaped strings without a shell. Element
loading diagnostics go to stderr. This internal protocol is separate from the
public JSON request schema; normal users launch the HTTP server.

## Verification

```bash
ctest --test-dir build --output-on-failure
# For browser tests (also builds TypeScript):
cd web
npm ci
npx playwright install chromium
npm test
```

Browser tests launch their own server at port 8765 and use the real C++ backend
at `../build/atom_api`. Set `QM_API_BINARY` to an absolute path to test another
build. Browser tests check H and Au results, actual WebGL pixels, current updates,
play/pause, stale runs, errors, offline recovery, integer validation, and mobile
layout. Software browser rendering is used for repeatable tests, with no GPU
performance claim. Screenshots and failure traces are in `web/test-results/`.
CTest includes HTTP/native integration tests whenever Python is available;
CI explicitly installs Python and runs those tests for console, OpenGL and CUDA
fallback builds. A separate frontend CI job builds the TypeScript UI and runs
browser tests; tagged release delivery also requires that job to pass.
