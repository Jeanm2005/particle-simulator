import { ApiFailure } from "./protocol.js";
let worker: Worker | null = null;
let nextId = 0;
const pending = new Map<number, { resolve(value: unknown): void; reject(reason: unknown): void; timeout: number }>();
export const browserEngine = document.querySelector<HTMLMetaElement>('meta[name="execution-mode"]')?.content === "wasm";
function reset(reason: Error): void {
  worker?.terminate(); worker = null;
  for (const call of pending.values()) { window.clearTimeout(call.timeout); call.reject(reason); }
  pending.clear();
}
export function browserApi<T>(path: string, body?: unknown): Promise<T> {
  if (!worker) {
    worker = new Worker(new URL("./wasm-worker.js", import.meta.url), { type: "module" });
    worker.onmessage = event => {
      const { id, payload } = event.data;
      const call = pending.get(id);
      if (!call) return;
      pending.delete(id); window.clearTimeout(call.timeout);
      if (payload.error) call.reject(new ApiFailure(payload.error.message, payload.error.field, payload.error.code));
      else call.resolve(payload);
    };
    worker.onerror = () => reset(new ApiFailure("Could not load the C++ browser engine. Reload or reconnect to try again.", "", "backend-unavailable"));
  }
  const id = ++nextId;
  return new Promise<T>((resolve, reject) => {
    const timeout = window.setTimeout(() => reset(new ApiFailure("The browser engine timed out. Reconnect and sample again.", "", "backend-unavailable")), 35000);
    pending.set(id, { resolve: value => resolve(value as T), reject, timeout });
    worker!.postMessage({ id, path, body });
  });
}
