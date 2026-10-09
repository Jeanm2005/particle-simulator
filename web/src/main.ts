import type { Catalog, Request, Result } from "./types.js";
import { CloudViewer } from "./viewer.js";

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
const fieldIds: Record<string, string> = { "element.atomicNumber": "atomic-number", "state.n": "n", "state.l": "l", "state.m": "m", "sampleCount": "samples", "screening": "screening", "seed": "seed", "backend": "backend", "model": "model", "dt": "dt" };
let catalog: Catalog | null = null;
let result: Result | null = null;
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

class ApiFailure extends Error {
  constructor(message: string, readonly field = "", readonly code = "") { super(message); }
}
async function api<T>(path: string, body?: unknown): Promise<T> {
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
  step.disabled = busy || !result || dirty || playing;
  play.disabled = !result || dirty || (busy && !playing);
  sample.textContent = busy ? "Computing…" : "Sample orbital ↗";
  play.textContent = playing ? "Pause" : "Play";
  play.setAttribute("aria-pressed", String(playing));
}
function pause(): void {
  playing = false; window.clearTimeout(timer); timer = undefined; updateButtons();
}
function value(id: string): number { return element<HTMLInputElement>(id).valueAsNumber; }
function selected(id: string): string { return element<HTMLSelectElement>(id).value; }
function request(): Request {
  return { contractVersion: catalog?.contractVersion ?? 1, model: selected("model"), element: { atomicNumber: value("atomic-number") }, state: { n: value("n"), l: value("l"), m: value("m") }, screening: selected("screening") as Request["screening"], sampleCount: value("samples"), seed: value("seed"), backend: selected("backend") as Request["backend"] };
}
function display(next: Result, reset: boolean): void {
  result = next;
  const q = next.request.state;
  const orbital = `${q.n}${"spdf"[q.l]}`;
  element("active-state").textContent = `${next.element.symbol} / ${orbital} / m = ${q.m}`;
  element("energy").textContent = next.energyEV.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("radius").textContent = next.meanRadiusA0.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("charge").textContent = next.effectiveCharge.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("count").textContent = next.actualSampleCount.toLocaleString();
  element("time").textContent = next.timeAtomicUnits.toLocaleString(undefined, { maximumFractionDigits: 4 });
  element("preview").textContent = `${next.previewCount.toLocaleString()} shown / ${next.actualSampleCount.toLocaleString()} sampled`;
  const info = next.operationExecution;
  element("execution").textContent = `Sampling: ${next.samplingExecution.backend.toUpperCase()}${next.samplingExecution.fallbackReason ? ` (${next.samplingExecution.fallbackReason})` : ""}. Latest operation: ${info.backend.toUpperCase()}${info.fallbackReason ? ` (${info.fallbackReason})` : ""}. Seed ${next.request.seed}.`;
  element("flow-description").textContent = q.m === 0 ? "m = 0: zero current. Stepping advances model time without moving samples." : "Rotation about Y preserves radius and height; density remains stationary.";
  const screening = next.request.screening === "pure-z" ? "Pure nuclear Z; stationary one-electron model." : "Slater screening is approximate and does not model electron rearrangement.";
  element("interpretation").textContent = `${screening} ${next.metadataAvailable ? "Element metadata awaits provenance review." : "No element metadata is available for this Z."} Points sample |ψ|²; color and brightness are display mappings, not measured observables. Mean radius is a sample estimate. Current flow does not show individual electron trajectories.`;
  element("empty").hidden = true;
  viewer?.setPoints(next.points, reset);
}
async function sampleState(): Promise<void> {
  if (busy || !form.reportValidity()) return;
  pause(); clearError(); busy = true; updateButtons();
  status.textContent = "Sampling the validated orbital…";
  try {
    const next = await api<Result>("/api/sample", request());
    dirty = false; display(next, true);
    status.textContent = `Ready. ${next.request.screening === "pure-z" ? "Pure Z" : "Approximate Slater screening"}; statistics use all samples.`;
  } catch (reason) { status.textContent = "Sampling failed. Previous results remain visible."; showError(reason); }
  finally { busy = false; updateButtons(); }
}
async function advance(): Promise<void> {
  if (busy || !result || dirty) return;
  const input = element<HTMLInputElement>("dt");
  if (!input.reportValidity() || !Number.isFinite(input.valueAsNumber)) { pause(); return; }
  clearError(); busy = true; updateButtons();
  try {
    display(await api<Result>("/api/advance", { runId: result.runId, dt: input.valueAsNumber }), false);
  } catch (reason) { showError(reason); }
  finally {
    busy = false; updateButtons();
    if (playing && !dirty) timer = window.setTimeout(() => void advance(), 100);
  }
}
function syncRanges(): void {
  const n = value("n"), l = value("l");
  element<HTMLInputElement>("l").max = String(Math.min(catalog?.maxL ?? 3, Number.isFinite(n) ? n - 1 : 3));
  element<HTMLInputElement>("m").min = String(-l);
  element<HTMLInputElement>("m").max = String(l);
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
  syncRanges(); status.textContent = result ? "Inputs changed. Sample orbital to apply; previous results remain visible." : "Ready to sample."; updateButtons();
});
step.addEventListener("click", () => void advance());
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
    syncRanges(); element("connection").textContent = "● Local backend connected";
    status.textContent = "Ready. Choose a state and sample the orbital.";
  } catch (reason) { showError(reason); reconnect.hidden = false; }
  finally { busy = false; updateButtons(); }
}
reconnect.addEventListener("click", () => void connect());
void connect();
