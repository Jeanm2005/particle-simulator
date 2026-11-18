import type { LatticeResult } from "./types.js";
// Draws omega(k) for both branches over the first Brillouin zone and marks the active mode.
export function drawDispersion(canvas: HTMLCanvasElement, chain: LatticeResult): void {
  const ratio = Math.min(window.devicePixelRatio || 1, 2);
  const width = Math.max(1, Math.round(canvas.clientWidth * ratio)), height = Math.max(1, Math.round(canvas.clientHeight * ratio));
  if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
  const w = width / ratio, h = height / ratio;
  const left = 46, right = 14, top = 12, bottom = 30;
  const top_omega = chain.opticalMax * 1.08;
  const x = (k: number) => left + (k + Math.PI) / (2 * Math.PI) * (w - left - right);
  const y = (omega: number) => h - bottom - omega / top_omega * (h - top - bottom);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#070e13"; ctx.fillRect(0, 0, w, h);
  if (chain.opticalMin - chain.acousticMax > 1e-9) {
    ctx.fillStyle = "rgba(246,192,128,0.07)";
    ctx.fillRect(left, y(chain.opticalMin), w - left - right, y(chain.acousticMax) - y(chain.opticalMin));
  }
  ctx.strokeStyle = "#263740"; ctx.lineWidth = 1; ctx.fillStyle = "#9baeb9"; ctx.font = "11px Inter, system-ui, sans-serif";
  ctx.textAlign = "center";
  for (const [k, label] of [[-Math.PI, "−π/a"], [0, "0"], [Math.PI, "π/a"]] as [number, string][]) {
    ctx.beginPath(); ctx.moveTo(x(k), top); ctx.lineTo(x(k), h - bottom); ctx.stroke();
    ctx.fillText(label, x(k), h - 10);
  }
  ctx.textAlign = "right";
  const tick = top_omega > 2 ? 1 : 0.5;
  for (let omega = 0; omega <= top_omega; omega += tick) {
    ctx.beginPath(); ctx.moveTo(left, y(omega)); ctx.lineTo(w - right, y(omega)); ctx.stroke();
    ctx.fillText(omega.toFixed(1), left - 8, y(omega) + 4);
  }
  for (const [index, color] of [[1, "#f6c080"], [2, "#3db0e3"]] as [1 | 2, string][]) {
    ctx.strokeStyle = color; ctx.lineWidth = 2; ctx.beginPath();
    chain.dispersion.forEach((point, i) => { if (i === 0) ctx.moveTo(x(point[0]), y(point[index])); else ctx.lineTo(x(point[0]), y(point[index])); });
    ctx.stroke();
  }
  ctx.fillStyle = "#ffffff";
  ctx.beginPath(); ctx.arc(x(chain.wavenumber), y(chain.angularFrequency), 5, 0, 2 * Math.PI); ctx.fill();
}
