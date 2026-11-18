export interface ElementInfo {
  atomicNumber: number;
  symbol: string;
  name: string;
  configuration: string;
  atomicMass: number | null;
}
export interface Request {
  contractVersion: number;
  model: string;
  element: { atomicNumber: number };
  state: { n: number; l: number; m: number };
  screening: "pure-z" | "slater-neutral";
  sampleCount: number;
  seed: number;
  backend: "cpu" | "auto";
  superposition?: { state: { n: number; l: number; m: number }; weight: number; phase: number };
}
export interface Execution { backend: string; fallbackReason: string }
export interface Result {
  runId: string;
  request: Request;
  element: ElementInfo;
  metadataAvailable: boolean;
  actualSampleCount: number;
  previewCount: number;
  effectiveCharge: number;
  energyEV: number;
  meanRadiusA0: number;
  meanYA0: number;
  timeAtomicUnits: number;
  samplingExecution: Execution;
  operationExecution: Execution;
  points: [number, number, number][];
}
export interface Sonification {
  mapping: string;
  mappingVersion: number;
  label: string;
  state: { n: number; l: number; m: number };
  effectiveCharge: number;
  durationSeconds: number;
  sampleRate: number;
  baseFrequencyHz: number;
  toneFrequencyHz: number;
  scanRadiusA0?: number;
  nodeRadiiA0?: number[];
  secondState?: { n: number; l: number; m: number };
  startTimeAtomicUnits?: number;
  bohrFrequencyAtomicUnits?: number;
  dipoleAmplitudeA0?: number;
  silentReason?: string;
  frames: number;
  peakFraction: number;
  mimeType: string;
  wavBase64: string;
}
export interface SonificationResponse { runId: string; sonification: Sonification }
export interface Catalog {
  contractVersion: number;
  model: string;
  maxN: number;
  maxL: number;
  maxSamples: number;
  maxPreviewPoints: number;
  maxSuperpositionN: number;
  maxSuperpositionSamples: number;
  elements: ElementInfo[];
}
