export class CloudViewer {
  private gl: WebGLRenderingContext;
  private program: WebGLProgram;
  private buffer: WebGLBuffer;
  private count = 0;
  private scale = 1;
  private yaw = 0.65;
  private pitch = 0.25;
  private distance = 3.2;
  private drag: { x: number; y: number } | null = null;
  private axis: WebGLBuffer;
  private disposed = false;

  constructor(private canvas: HTMLCanvasElement) {
    const gl = canvas.getContext("webgl", { antialias: true, alpha: false });
    if (!gl) throw new Error("WebGL is unavailable. Numerical results are still available; enable browser graphics to view the cloud.");
    this.gl = gl;
    const vertex = this.shader(gl.VERTEX_SHADER, `
      attribute vec3 point;
      uniform float scale, yaw, pitch, distance, aspect;
      uniform mediump float axes;
      varying mediump float radius;
      void main() {
        vec3 p = point / scale;
        radius = length(p);
        p = vec3(cos(yaw)*p.x + sin(yaw)*p.z, p.y, -sin(yaw)*p.x + cos(yaw)*p.z);
        p = vec3(p.x, cos(pitch)*p.y - sin(pitch)*p.z, sin(pitch)*p.y + cos(pitch)*p.z);
        float depth = distance - p.z;
        gl_Position = vec4(p.x*2.414/aspect, p.y*2.414, 0.98*depth-0.1, depth);
        gl_PointSize = axes > 0.5 ? 1.0 : 2.2;
      }`);
    const fragment = this.shader(gl.FRAGMENT_SHADER, `
      precision mediump float;
      uniform float axes;
      varying float radius;
      void main() {
        if (axes > 0.5) { gl_FragColor = vec4(0.40,0.58,0.67,0.4); return; }
        float d = length(gl_PointCoord - vec2(0.5));
        if (d > 0.5) discard;
        vec3 color = mix(vec3(1.0,0.73,0.35),vec3(0.24,0.69,0.89),clamp(radius*1.4,0.0,1.0));
        gl_FragColor = vec4(color, 0.36*(1.0-d));
      }`);
    const program = gl.createProgram();
    if (!program) throw new Error("Could not create the cloud renderer");
    gl.attachShader(program, vertex); gl.attachShader(program, fragment); gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error("Could not link the cloud renderer");
    this.program = program;
    gl.deleteShader(vertex); gl.deleteShader(fragment);
    const buffer = gl.createBuffer(), axis = gl.createBuffer();
    if (!buffer || !axis) throw new Error("Could not allocate the cloud renderer");
    this.buffer = buffer; this.axis = axis;
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(), gl.DYNAMIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER, axis);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1.2,0,0,1.2,0,0,0,-1.2,0,0,1.2,0,0,0,-1.2,0,0,1.2]), gl.STATIC_DRAW);
    canvas.addEventListener("pointerdown", event => {
      canvas.setPointerCapture(event.pointerId); this.drag = { x: event.clientX, y: event.clientY };
    });
    canvas.addEventListener("pointermove", event => {
      if (!this.drag) return;
      this.yaw += (event.clientX - this.drag.x) * 0.008;
      this.pitch = Math.max(-1.5, Math.min(1.5, this.pitch + (event.clientY - this.drag.y) * 0.008));
      this.drag = { x: event.clientX, y: event.clientY }; this.draw();
    });
    canvas.addEventListener("pointerup", () => { this.drag = null; });
    canvas.addEventListener("pointercancel", () => { this.drag = null; });
    canvas.addEventListener("wheel", event => {
      event.preventDefault(); this.zoom(Math.exp(event.deltaY * 0.001));
    }, { passive: false });
    canvas.addEventListener("keydown", event => {
      if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "+", "-", "="].includes(event.key)) return;
      event.preventDefault();
      if (event.key === "ArrowLeft") this.yaw -= 0.1;
      if (event.key === "ArrowRight") this.yaw += 0.1;
      if (event.key === "ArrowUp") this.pitch = Math.min(1.5, this.pitch + 0.1);
      if (event.key === "ArrowDown") this.pitch = Math.max(-1.5, this.pitch - 0.1);
      if (event.key === "+" || event.key === "=") this.zoom(0.9);
      if (event.key === "-") this.zoom(1.1);
      this.draw();
    });
    canvas.addEventListener("webglcontextlost", event => {
      event.preventDefault(); this.disposed = true;
      canvas.dispatchEvent(new CustomEvent("renderer-error", { detail: "Browser graphics context was lost. Reload to restore the viewer; numerical results remain available." }));
    });
    new ResizeObserver(() => this.draw()).observe(canvas);
    this.draw();
  }
  private shader(type: number, source: string): WebGLShader {
    const shader = this.gl.createShader(type);
    if (!shader) throw new Error("Could not compile the cloud renderer");
    this.gl.shaderSource(shader, source); this.gl.compileShader(shader);
    if (!this.gl.getShaderParameter(shader, this.gl.COMPILE_STATUS)) throw new Error("Could not compile the cloud renderer");
    return shader;
  }
  setPoints(points: [number, number, number][], reset: boolean): void {
    const flat = new Float32Array(points.length * 3);
    let maximum = 0;
    points.forEach((point, i) => { flat.set(point, i * 3); maximum = Math.max(maximum, Math.hypot(...point)); });
    this.scale = maximum || 1;
    this.count = points.length;
    this.gl.bindBuffer(this.gl.ARRAY_BUFFER, this.buffer);
    this.gl.bufferData(this.gl.ARRAY_BUFFER, flat, this.gl.DYNAMIC_DRAW);
    if (reset) this.reset(); else this.draw();
  }
  reset(): void { this.yaw = 0.65; this.pitch = 0.25; this.distance = 3.2; this.draw(); }
  zoom(factor: number): void { this.distance = Math.max(1.4, Math.min(15, this.distance * factor)); this.draw(); }
  private draw(): void {
    if (this.disposed) return;
    const gl = this.gl;
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(this.canvas.clientWidth * ratio));
    const height = Math.max(1, Math.round(this.canvas.clientHeight * ratio));
    if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; }
    gl.viewport(0, 0, width, height);
    gl.clearColor(0.027, 0.055, 0.075, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(this.program);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
    const uniform = (name: string, value: number) => gl.uniform1f(gl.getUniformLocation(this.program, name), value);
    uniform("yaw", this.yaw); uniform("pitch", this.pitch); uniform("distance", this.distance); uniform("aspect", width / height);
    const attribute = gl.getAttribLocation(this.program, "point");
    gl.enableVertexAttribArray(attribute);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.axis);
    gl.vertexAttribPointer(attribute, 3, gl.FLOAT, false, 0, 0);
    uniform("scale", 1); uniform("axes", 1); gl.drawArrays(gl.LINES, 0, 6);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
    gl.vertexAttribPointer(attribute, 3, gl.FLOAT, false, 0, 0);
    uniform("scale", this.scale); uniform("axes", 0); gl.drawArrays(gl.POINTS, 0, this.count);
  }
}
