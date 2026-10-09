import type { Request } from "./types.js";
export class ApiFailure extends Error {
  constructor(message: string, readonly field = "", readonly code = "") { super(message); }
}
function whole(value: number, field: string, min = -2147483648, max = 2147483647): string {
  if (!Number.isSafeInteger(value) || value < min || value > max) throw new ApiFailure("Expected an in-range whole integer", field, "invalid-input");
  return String(value);
}
function real(value: number, field: string): string {
  if (!Number.isFinite(value)) throw new ApiFailure("Expected a finite number", field, "invalid-input");
  return String(value);
}
function text(value: string, field: string): string {
  if (typeof value !== "string" || value.length > 256 || /[\x00-\x1f]/.test(value)) throw new ApiFailure("Expected a short string", field, "invalid-input");
  return '"' + value.replaceAll('\\', '\\\\').replaceAll('"', '\\"') + '"';
}
export function encodeSample(request: Request): string {
  const q = request.state;
  const command = ["sample", whole(request.contractVersion, "contractVersion"), text(request.model, "model"), whole(request.element.atomicNumber, "element.atomicNumber"), whole(q.n, "state.n"), whole(q.l, "state.l"), whole(q.m, "state.m"), text(request.screening, "screening"), whole(request.sampleCount, "sampleCount", 1, 1000000), whole(request.seed, "seed", 0, 4294967295), text(request.backend, "backend")];
  if (request.model === "orbital-superposition") {
    const c = request.superposition;
    if (!c) throw new ApiFailure("Supply the second orbital", "superposition.state", "invalid-input");
    command.push(whole(c.state.n, "superposition.state.n"), whole(c.state.l, "superposition.state.l"), whole(c.state.m, "superposition.state.m"), real(c.weight, "superposition.weight"), real(c.phase, "superposition.phase"));
  }
  return command.join(" ");
}
