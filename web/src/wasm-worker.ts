import createPhysics from "./physics.mjs";
import { encodeSample, ApiFailure } from "./protocol.js";
import type { Request } from "./types.js";
const scope = self as unknown as { onmessage: ((event: MessageEvent) => void) | null; postMessage(value: unknown): void };
const ready = createPhysics({ locateFile: path => new URL(path, import.meta.url).href }).then(module => new module.BrowserSession());
let runId: string | null = null;
scope.onmessage = async event => {
  const { id, path, body } = event.data as { id: number; path: string; body?: unknown };
  try {
    const session = await ready;
    let command: string;
    if (path === "/api/catalog") command = "catalog";
    else if (path === "/api/sample") command = encodeSample(body as Request);
    else if (path === "/api/advance") {
      const advance = body as { runId: string; dt: number };
      if (!runId || advance.runId !== runId) throw new ApiFailure("This run is no longer active; sample again", "runId", "invalid-input");
      if (!Number.isFinite(advance.dt)) throw new ApiFailure("Expected finite atomic-unit time", "dt", "invalid-input");
      command = `advance ${advance.dt}`;
    } else throw new ApiFailure("Unknown operation", "", "invalid-input");
    const payload = JSON.parse(session.execute(command)) as Record<string, unknown>;
    if (!payload.error) {
      if (path === "/api/sample") runId = crypto.randomUUID();
      if (path !== "/api/catalog") payload.runId = runId;
    }
    scope.postMessage({ id, payload });
  } catch (error) {
    scope.postMessage({ id, payload: { error: { code: error instanceof ApiFailure ? error.code : "backend-unavailable", field: error instanceof ApiFailure ? error.field : "", message: error instanceof Error ? error.message : "Could not initialize the C++ browser engine" } } });
  }
};
