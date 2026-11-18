import type { Catalog, LatticeRequest, LatticeResult, Request, Result, SonificationResponse } from "./types.js";
import { drawDispersion } from "./dispersion.js";
import { CloudViewer } from "./viewer.js";
import { browserApi, browserEngine } from "./browser-api.js";
import { ApiFailure } from "./protocol.js";

function element<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (!node) throw new Error(`Missing UI element ${id}`);
  return node as T;
}
const form = element<HTMLFormElement>("state-form");
const setup = element<HTMLFieldSetElement>("setup");
const sample = element<HTMLButtonElement>("sample");
const step = element<HTMLButtonElement>("step");
const play = element<HTMLButtonElement>("play");
const status = element("status"), error = element("error");
const reconnect = element<HTMLButtonElement>("reconnect");
const sonifyButton = element<HTMLButtonElement>("sonify");
const audio = element<HTMLAudioElement>("audio");
const download = element<HTMLAnchorElement>("download");
let audioUrl: string | null = null;
const fieldIds: Record<string, string> = { "element.atomicNumber": "atomic-number", "state.n": "n", "state.l": "l", "state.m": "m", "sampleCount": "samples", "screening": "screening", "seed": "seed", "backend": "backend", "model": "model", "dt": "dt" };
Object.assign(fieldIds, { "lattice.cells": "cells", "lattice.massRatio": "mass-ratio", "lattice.branch": "branch", "lattice.modeIndex": "mode-index", "lattice.amplitude": "amplitude" });
Object.assign(fieldIds, { "sonification.durationSeconds": "duration", "sonification.baseFrequencyHz": "tone" });
Object.assign(fieldIds, { "superposition.state": "second-n", "superposition.state.n": "second-n", "superposition.state.l": "second-l", "superposition.state.m": "second-m", "superposition.weight": "weight", "superposition.phase": "phase" });
let catalog: Catalog | null = null;
let result: Result | null = null;
let chain: LatticeResult | null = null;
const dispersionCanvas = element<HTMLCanvasElement>("dispersion");
new ResizeObserver(() => { if (chain) drawDispersion(dispersionCanvas, chain); }).observe(dispersionCanvas);
let busy = false, playing = false, dirty = false;
let timer: number | undefined;
let viewer: CloudViewer | null = null;
const canvas = element<HTMLCanvasElement>("cloud");
function rendererError(message: string): void {
  element("renderer-error").hidden = false; element("renderer-error").textContent = message;
}
try { viewer = new CloudViewer(canvas); }
catch (reason) { rendererError(reason instanceof Error ? reason.message : "Could not start the cloud renderer"); }
canvas.addEventListener("renderer-error", event => rendererError((event as CustomEvent<string>).detail));

async function api<T>(path: string, body?: unknown): Promise<T> {
  if (browserEngine) return browserApi<T>(path, body);
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), 35000);
  try {
    const response = await fetch(path, { method: body === undefined ? "GET" : "POST", headers: body === undefined ? {} : { "Content-Type": "application/json" }, body: body === undefined ? undefined : JSON.stringify(body), signal: controller.signal });
    const payload = await response.json() as T & { error?: { message: string; field: string; code: string } };
    if (!response.ok || payload.error) throw new ApiFailure(payload.error?.message || `Request failed (${response.status})`, payload.error?.field, payload.error?.code);
    return payload;
  } catch (reason) {
    if (reason instanceof ApiFailure) throw reason;
    throw new ApiFailure("Could not reach the local backend. Check that tools/serve.py is running, then reconnect.", "", "backend-unavailable");
  } finally { window.clearTimeout(timeout); }
}
function clearError(): void {
  error.hidden = true; error.textContent = "";
  for (const id of Object.values(fieldIds)) element(id).removeAttribute("aria-invalid");
}
function showError(reason: unknown): void {
  pause();
  const failure = reason instanceof Error ? reason : new Error(String(reason));
  error.textContent = failure.message; error.hidden = false;
  if (reason instanceof ApiFailure) {
    const id = fieldIds[reason.field];
    if (id) { element(id).setAttribute("aria-invalid", "true"); element(id).focus(); }
    if (reason.field === "runId" || reason.code === "backend-unavailable") {
      dirty = true;
      status.textContent = "Sample again to start an active run.";
    }
    if (reason.code === "backend-unavailable") {
      element("connection").textContent = "Backend disconnected"; reconnect.hidden = false;
    }
  }
}
function updateButtons(): void {
  setup.disabled = busy || catalog === null;
  const active = result ?? chain;
  step.disabled = busy || !active || dirty || playing;
  play.disabled = !active || dirty || (busy && !playing);
  sonifyButton.disabled = busy || !result || dirty || playing;
  sample.textContent = busy ? "Computing…" : selected("model") === "lattice-phonons" ? "Compute normal mode ↗" : "Sample orbital ↗";
  play.textContent = playing ? "Pause" : "Play";
  play.setAttribute("aria-pressed", String(playing));
}
function pause(): void {
  playing = false; window.clearTimeout(timer); timer = undefined; updateButtons();
}
function value(id: string): number { return element<HTMLInputElement>(id).valueAsNumber; }
function selected(id: string): string { return element<HTMLSelectElement>(id).value; }
function request(): Request {
  const request: Request = { contractVersion: catalog?.contractVersion ?? 1, model: selected("model"), element: { atomicNumber: value("atomic-number") }, state: { n: value("n"), l: value("l"), m: value("m") }, screening: selected("screening") as Request["screening"], sampleCount: value("samples"), seed: value("seed"), backend: selected("backend") as Request["backend"] };
  if (request.model === "orbital-superposition") request.superposition = { state: { n: value("second-n"), l: value("second-l"), m: value("second-m") }, weight: value("weight"), phase: value("phase") };
  return request;
}
function latticeRequest(): LatticeRequest {
  return { contractVersion: catalog?.contractVersion ?? 1, model: "lattice-phonons", lattice: { cells: value("cells"), massRatio: value("mass-ratio"), branch: selected("branch") as "acoustic" | "optical", modeIndex: value("mode-index"), amplitude: value("amplitude") } };
}
function showModelPanels(lattice: boolean): void {
  element("orbital-metrics").hidden = lattice;
  element("lattice-metrics").hidden = !lattice;
  element("dispersion-panel").hidden = !lattice;
  element("sonify-panel").hidden = lattice;
  element("view-caption").textContent = lattice ? "X along the chain · Y = displacement × display gain · lattice constants" : "Y polar axis · coordinates in a₀";
  element("time-unit").textContent = lattice ? "reduced units √(m₁/K)" : "atomic units";
  element("dt-unit").textContent = lattice ? "reduced units" : "atomic units";
  element("view-eyebrow").textContent = lattice ? "ATOMIC DISPLACEMENTS" : "ELECTRON PROBABILITY DENSITY";
  viewer?.setChainMode(lattice);
}
function displayLattice(next: LatticeResult, reset: boolean): void {
  chain = next; result = null;
  showModelPanels(true);
  const c = next.request.lattice;
  const fixed = (x: number, digits = 4) => x.toLocaleString(undefined, { maximumFractionDigits: digits });
  element("active-state").textContent = `Diatomic chain / ${c.branch} / j = ${c.modeIndex} of ${c.cells}`;
  element("omega").textContent = fixed(next.angularFrequency);
  element("wavenumber").textContent = fixed(next.wavenumber / Math.PI);
  element("group-velocity").textContent = fixed(next.groupVelocity);
  element("band-gap").textContent = next.opticalMin - next.acousticMax > 1e-9 ? `${fixed(next.acousticMax, 3)}–${fixed(next.opticalMin, 3)}` : "none";
  element("time").textContent = fixed(next.timeReduced);
  element("preview").textContent = `${next.atomCount.toLocaleString()} atoms · display gain ×${fixed(next.displayGain, 1)}`;
  element("dynamics-title").textContent = "Phonon normal mode";
  element("flow-description").textContent = next.angularFrequency < 1e-9 ? "k = 0 acoustic mode: a uniform translation with zero frequency." : "Exact traveling normal mode; each step evaluates u(n, t) analytically, so no integration error accumulates.";
  element("execution").textContent = `Analytic normal mode in C++. Sound speed ${fixed(next.soundSpeed)} a√(K/m₁); optical maximum ${fixed(next.opticalMax)}.`;
  element("interpretation").textContent = `Harmonic nearest-neighbour springs with periodic boundaries in reduced units (K = m₁ = a = 1). Displacements are longitudinal; the vertical offset is a ×${fixed(next.displayGain, 1)} display gain, not a transverse motion. No anharmonicity, damping, temperature, or three-dimensional structure.`;
  element("empty").hidden = true;
  viewer?.setPoints(next.points, reset);
  drawDispersion(dispersionCanvas, next);
}
function display(next: Result, reset: boolean): void {
  result = next; chain = null;
  showModelPanels(false);
  const q = next.request.state;
  const orbital = `${q.n}${"spdf"[q.l]}`;
  const coherent = next.request.superposition;
  element("active-state").textContent = coherent ? `${next.element.symbol} / ${orbital} + ${coherent.state.n}${"spdf"[coherent.state.l]} / interference` : `${next.element.symbol} / ${orbital} / m = ${q.m}`;
  element("energy").textContent = next.energyEV.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("radius").textContent = next.meanRadiusA0.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("centroid-metric").hidden = !coherent;
  element("centroid").textContent = next.meanYA0.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("dynamics-title").textContent = coherent ? "Time-dependent interference" : "Probability current";
  element("charge").textContent = next.effectiveCharge.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("count").textContent = next.actualSampleCount.toLocaleString();
  element("time").textContent = next.timeAtomicUnits.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("preview").textContent = `${next.previewCount.toLocaleString()} shown / ${next.actualSampleCount.toLocaleString()} sampled`;
  const info = next.operationExecution;
  element("execution").textContent = `Sampling: ${next.samplingExecution.backend.toUpperCase()}${next.samplingExecution.fallbackReason ? ` (${next.samplingExecution.fallbackReason})` : ""}. Latest operation: ${info.backend.toUpperCase()}${info.fallbackReason ? ` (${info.fallbackReason})` : ""}. Seed ${next.request.seed}.`;
  element("flow-description").textContent = coherent ? "Relative energy phases evolve; each step samples a new density snapshot." : q.m === 0 ? "m = 0: zero current. Stepping advances model time without moving samples." : "Rotation about Y preserves radius and height; density remains stationary.";
  const screening = next.request.screening === "pure-z" ? "Pure nuclear Z; stationary one-electron model." : "Slater screening is approximate and does not model electron rearrangement.";
  element("interpretation").textContent = `${coherent ? "Normalized two-state superposition in one pure-Z Hamiltonian. Energy is an expectation value; density snapshots are resampled, not tracked trajectories." : screening} ${next.metadataAvailable ? "Element metadata is sourced from CIAAW and NIST." : "No element metadata is available for this Z."} Points sample |ψ|²; color and brightness are display mappings, not measured observables. Mean radius and mean Y are sample estimates.`;
  element("empty").hidden = true;
  viewer?.setPoints(next.points, reset);
}
async function sampleState(): Promise<void> {
  if (busy || !form.reportValidity()) return;
  pause(); clearError(); busy = true; updateButtons();
  if (selected("model") === "lattice-phonons") {
    status.textContent = "Computing the normal mode…";
    try {
      const next = await api<LatticeResult>("/api/lattice", latticeRequest());
      dirty = false; clearAudio(""); displayLattice(next, true);
      status.textContent = "Ready. Exact harmonic normal mode; step or play to evolve it.";
    } catch (reason) { status.textContent = "Computation failed. Previous results remain visible."; showError(reason); }
    finally { busy = false; updateButtons(); }
    return;
  }
  status.textContent = "Sampling the validated orbital…";
  try {
    const next = await api<Result>("/api/sample", request());
    dirty = false; display(next, true);
    clearAudio(next.request.superposition ? "Ready to sonify the interference: the tone follows the oscillating dipole ⟨y⟩(t)." : "Ready to generate audio for this orbital.");
    element("sonify-title").textContent = next.request.superposition ? "Interference sonification" : "Density sonification";
    element("sonify-description").textContent = next.request.superposition ? "Pitch is proportional to the Bohr frequency; the waveform follows ⟨y⟩(t) from the current model time." : "Amplitude follows radial density r²|R|² from the nucleus outward; silences mark radial nodes.";
    status.textContent = `Ready. ${next.request.superposition ? "Coherent two-state density" : next.request.screening === "pure-z" ? "Pure Z" : "Approximate Slater screening"}; statistics use all samples.`;
  } catch (reason) { status.textContent = "Sampling failed. Previous results remain visible."; showError(reason); }
  finally { busy = false; updateButtons(); }
}
async function advance(): Promise<void> {
  const active = result ?? chain;
  if (busy || !active || dirty) return;
  const input = element<HTMLInputElement>("dt");
  if (!input.reportValidity() || !Number.isFinite(input.valueAsNumber)) { pause(); return; }
  clearError(); busy = true; updateButtons();
  try {
    if (chain) displayLattice(await api<LatticeResult>("/api/advance", { runId: active.runId, dt: input.valueAsNumber }), false);
    else display(await api<Result>("/api/advance", { runId: active.runId, dt: input.valueAsNumber }), false);
  } catch (reason) { showError(reason); }
  finally {
    busy = false; updateButtons();
    if (playing && !dirty) timer = window.setTimeout(() => void advance(), 100);
  }
}
function clearAudio(message: string): void {
  if (audioUrl) URL.revokeObjectURL(audioUrl);
  audioUrl = null; audio.pause(); audio.removeAttribute("src"); audio.hidden = true;
  download.hidden = true; download.removeAttribute("href");
  element("sonify-result").textContent = message;
}
async function sonify(): Promise<void> {
  if (busy || !result || dirty) return;
  const duration = element<HTMLInputElement>("duration"), tone = element<HTMLInputElement>("tone");
  if (!duration.reportValidity() || !tone.reportValidity()) return;
  pause(); clearError(); busy = true; updateButtons();
  element("sonify-result").textContent = "Generating audio in C++…";
  try {
    const { sonification: s } = await api<SonificationResponse>("/api/sonify", { runId: result.runId, durationSeconds: duration.valueAsNumber, sampleRate: 44100, baseFrequencyHz: tone.valueAsNumber });
    const bytes = Uint8Array.from(atob(s.wavBase64), c => c.charCodeAt(0));
    clearAudio("");
    audioUrl = URL.createObjectURL(new Blob([bytes], { type: s.mimeType }));
    audio.src = audioUrl; audio.hidden = false;
    download.href = audioUrl; download.hidden = false;
    const orbital = (q: { n: number; l: number }) => `${q.n}${"spdf"[q.l]}`;
    const digits = { maximumFractionDigits: 3 };
    if (s.secondState) {
      download.download = `${result.element.symbol}_${orbital(s.state)}+${orbital(s.secondState)}_interference.wav`;
      const reason = s.silentReason === "dipole-forbidden" ? "Silent: these states have no Y dipole coupling (selection rule)." : s.silentReason === "no-coherence" ? "Silent: one state has zero weight, so nothing oscillates." : `Dipole ⟨y⟩ oscillates with amplitude ${s.dipoleAmplitudeA0!.toLocaleString(undefined, digits)} a₀.`;
      element("sonify-result").textContent = `${reason} Bohr frequency ${Math.abs(s.bohrFrequencyAtomicUnits!).toLocaleString(undefined, { maximumFractionDigits: 4 })} Eₕ → ${s.toneFrequencyHz.toLocaleString(undefined, { maximumFractionDigits: 1 })} Hz tone, starting at t = ${s.startTimeAtomicUnits!.toLocaleString(undefined, digits)} atomic units. ${s.label}`;
    } else {
      download.download = `${result.element.symbol}_${orbital(s.state)}_density_scan.wav`;
      const radii = s.nodeRadiiA0 ?? [];
      const nodes = radii.length === 0 ? "No radial nodes" : `${radii.length} ${radii.length === 1 ? "silence" : "silences"} at ${radii.map(r => r.toFixed(3)).join(", ")} a₀`;
      element("sonify-result").textContent = `${nodes}. Scan 0 → ${(s.scanRadiusA0 ?? 0).toLocaleString(undefined, digits)} a₀ over ${s.durationSeconds} s at ${s.sampleRate / 1000} kHz, ${s.baseFrequencyHz} Hz tone. ${s.label}`;
    }
  } catch (reason) { element("sonify-result").textContent = "Audio generation failed."; showError(reason); }
  finally { busy = false; updateButtons(); }
}
function syncRanges(): void {
  const lattice = selected("model") === "lattice-phonons";
  element("limits-orbital").hidden = lattice;
  element("limits-orbital-2").hidden = lattice;
  element("limits-lattice").hidden = !lattice;
  element("orbital-controls").hidden = lattice;
  (element("orbital-controls") as HTMLFieldSetElement).disabled = lattice;
  element("lattice-controls").hidden = !lattice;
  (element("lattice-controls") as HTMLFieldSetElement).disabled = !lattice;
  const cells = value("cells");
  if (Number.isFinite(cells)) {
    element<HTMLInputElement>("mode-index").min = String(Math.floor(-cells / 2) + 1);
    element<HTMLInputElement>("mode-index").max = String(Math.floor(cells / 2));
  }
  const coherent = selected("model") === "orbital-superposition";
  const maxN = coherent ? catalog?.maxSuperpositionN ?? 4 : catalog?.maxN ?? 7;
  element<HTMLInputElement>("n").max = String(maxN);
  element<HTMLInputElement>("samples").max = String(coherent ? catalog?.maxSuperpositionSamples ?? 20000 : catalog?.maxSamples ?? 1000000);
  element("superposition-controls").hidden = !coherent;
  for (const id of ["second-n", "second-l", "second-m", "weight", "phase"]) element<HTMLInputElement>(id).disabled = !coherent;
  element<HTMLSelectElement>("screening").disabled = coherent;
  const n = value("n"), l = value("l");
  element<HTMLInputElement>("l").max = String(Math.min(catalog?.maxL ?? 3, Number.isFinite(n) ? n - 1 : 3));
  element<HTMLInputElement>("m").min = String(-l);
  element<HTMLInputElement>("m").max = String(l);
  element<HTMLInputElement>("second-l").max = String(Math.min(3, value("second-n") - 1));
  element<HTMLInputElement>("second-m").min = String(-value("second-l"));
  element<HTMLInputElement>("second-m").max = String(value("second-l"));
}
form.addEventListener("submit", event => { event.preventDefault(); void sampleState(); });
form.addEventListener("input", event => {
  pause(); dirty = true; clearError();
  if ((event.target as HTMLElement).id === "element") {
    const z = selected("element");
    if (z !== "custom") element<HTMLInputElement>("atomic-number").value = z;
  }
  if ((event.target as HTMLElement).id === "atomic-number") {
    const z = String(value("atomic-number"));
    element<HTMLSelectElement>("element").value = catalog?.elements.some(e => String(e.atomicNumber) === z) ? z : "custom";
  }
  if ((event.target as HTMLElement).id === "model" && selected("model") === "lattice-phonons") element<HTMLInputElement>("dt").value = "0.5";
  if ((event.target as HTMLElement).id === "model" && selected("model") === "orbital-superposition") {
    element<HTMLSelectElement>("screening").value = "pure-z";
    element<HTMLInputElement>("samples").value = "2000";
    element<HTMLInputElement>("dt").value = "1";
    if (value("n") > 4) element<HTMLInputElement>("n").value = "1";
  }
  syncRanges(); status.textContent = result || chain ? "Inputs changed. Compute again to apply; previous results remain visible." : "Ready to sample."; updateButtons();
});
step.addEventListener("click", () => void advance());
sonifyButton.addEventListener("click", () => void sonify());
play.addEventListener("click", () => { if (playing) pause(); else { playing = true; updateButtons(); void advance(); } });
element("reset-camera").addEventListener("click", () => viewer?.reset());
document.addEventListener("visibilitychange", () => { if (document.hidden) pause(); });
async function connect(): Promise<void> {
  if (busy) return;
  busy = true; pause(); clearError(); reconnect.hidden = true; updateButtons();
  try {
    catalog = await api<Catalog>("/api/catalog");
    const select = element<HTMLSelectElement>("element");
    const currentZ = String(value("atomic-number"));
    select.replaceChildren(...catalog.elements.map(e => new Option(`${e.symbol} — ${e.name}`, String(e.atomicNumber))), new Option("Custom atomic number", "custom"));
    select.value = catalog.elements.some(e => String(e.atomicNumber) === currentZ) ? currentZ : "custom";
    element<HTMLInputElement>("samples").max = String(catalog.maxSamples);
    element<HTMLInputElement>("n").max = String(catalog.maxN);
    syncRanges(); element("connection").textContent = browserEngine ? "C++ browser engine connected" : "Local backend connected";
    if (browserEngine) {
      element<HTMLSelectElement>("backend").value = "cpu";
      element<HTMLSelectElement>("backend").options[0]!.disabled = true;
      element("execution-label").textContent = "C++ / WEBASSEMBLY · PROVISIONAL DEMO";
    }
    status.textContent = "Ready. Choose a state and sample the orbital.";
  } catch (reason) { showError(reason); reconnect.hidden = false; }
  finally { busy = false; updateButtons(); }
}
reconnect.addEventListener("click", () => void connect());
element("try-interference").addEventListener("click", () => {
  if (busy || !catalog) return;
  pause();
  element<HTMLSelectElement>("model").value = "orbital-superposition";
  element<HTMLSelectElement>("element").value = "1";
  element<HTMLSelectElement>("screening").value = "pure-z";
  for (const [id, number] of Object.entries({ "atomic-number": 1, n: 1, l: 0, m: 0, "second-n": 2, "second-l": 1, "second-m": 0, weight: 0.5, phase: 0, samples: 2000, dt: 1 })) element<HTMLInputElement>(id).value = String(number);
  syncRanges(); dirty = true; void sampleState();
});
void connect();
