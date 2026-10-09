#!/usr/bin/env python3
"""Same-origin loopback UI server; JSON framing only, physics stays in C++."""
import argparse
import json
import math
import os
from pathlib import Path
import selectors
import signal
import subprocess
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
MAX_BODY = 8192
MAX_RESPONSE = 8 * 1024 * 1024


class ApiError(Exception):
    def __init__(self, message, field="", code="invalid-input", status=400):
        super().__init__(message)
        self.payload = {"error": {"code": code, "field": field, "message": message}}
        self.status = status


def fields(value, expected, field=""):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ApiError("Supply exactly these fields: " + ", ".join(expected), field)


def whole(value, low, high, field):
    if type(value) is not int or not low <= value <= high:
        raise ApiError("Expected an in-range whole integer", field)
    return str(value)


def text(value, field):
    if not isinstance(value, str) or len(value) > 256 or any(ord(c) < 32 for c in value):
        raise ApiError("Expected a short string without control characters", field)
    # std::quoted in the native protocol escapes quotes and backslashes.
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def sample_command(value):
    keys = ("contractVersion", "model", "element", "state", "screening", "sampleCount", "seed", "backend")
    coherent = isinstance(value, dict) and value.get("model") == "orbital-superposition"
    fields(value, keys + (("superposition",) if coherent else ()))
    fields(value["element"], ("atomicNumber",), "element")
    fields(value["state"], ("n", "l", "m"), "state")
    # These checks prevent lossy conversions. Semantic validation belongs to OrbitalModel.
    command = " ".join(("sample", whole(value["contractVersion"], -2**31, 2**31-1, "contractVersion"),
                     text(value["model"], "model"),
                     whole(value["element"]["atomicNumber"], -2**31, 2**31-1, "element.atomicNumber"),
                     *(whole(value["state"][key], -2**31, 2**31-1, "state." + key) for key in ("n", "l", "m")),
                     text(value["screening"], "screening"),
                     whole(value["sampleCount"], -2**63, 2**63-1, "sampleCount"),
                     whole(value["seed"], 0, 2**32-1, "seed"), text(value["backend"], "backend")))
    if coherent:
        combination = value["superposition"]
        fields(combination, ("state", "weight", "phase"), "superposition")
        fields(combination["state"], ("n", "l", "m"), "superposition.state")
        command += " " + " ".join(whole(combination["state"][key], -2**31, 2**31-1, "superposition.state." + key) for key in ("n", "l", "m"))
        for key in ("weight", "phase"):
            value = combination[key]
            if type(value) not in (int, float) or abs(value) > 1e6 or not math.isfinite(value):
                raise ApiError("Expected a finite number with magnitude <= 1000000", "superposition." + key)
            command += " " + repr(value)
    return command


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ApiError("Duplicate JSON field", key)
        result[key] = value
    return result


def reject_constant(value):
    raise ApiError("Nonfinite JSON number: " + value)


class Worker:
    def __init__(self, binary, timeout=30):
        self.binary = str(Path(binary).resolve())
        self.timeout = timeout
        self.process = None
        self.lock = threading.Lock()
        self.run_id = None

    def close(self):
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        self.run_id = None

    def exchange(self, command):
        try:
            if self.process is None:
                self.process = subprocess.Popen([self.binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
            self.process.stdin.write((command + "\n").encode("utf-8"))
            response = bytearray()
            deadline = time.monotonic() + self.timeout
            with selectors.DefaultSelector() as selector:
                selector.register(self.process.stdout, selectors.EVENT_READ)
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        raise TimeoutError("Native worker timed out; sample again")
                    chunk = os.read(self.process.stdout.fileno(), 65536)
                    if not chunk:
                        raise RuntimeError("Native worker stopped; sample again")
                    response.extend(chunk)
                    if len(response) > MAX_RESPONSE:
                        raise RuntimeError("Native response exceeds transport limit")
                    if response.endswith(b"\n"):
                        break
            value = json.loads(response, parse_constant=reject_constant)
            if "error" in value:
                status = 422 if value["error"]["code"] != "resource-limit" else 413
                error = ApiError(value["error"]["message"], value["error"]["field"], value["error"]["code"], status)
                raise error
            return value
        except ApiError:
            raise
        except (OSError, RuntimeError, TimeoutError, ValueError) as error:
            self.close()
            raise ApiError(str(error), code="backend-unavailable", status=503) from error

    def request(self, route, value=None):
        if not self.lock.acquire(blocking=False):
            raise ApiError("The backend is busy; try again", code="backend-busy", status=409)
        try:
            if route == "/api/catalog":
                return self.exchange("catalog")
            if route == "/api/sample":
                result = self.exchange(sample_command(value))
                self.run_id = uuid.uuid4().hex
            elif route == "/api/advance":
                fields(value, ("runId", "dt"))
                if not isinstance(value["runId"], str) or self.run_id is None or value["runId"] != self.run_id:
                    raise ApiError("This run is no longer active; sample again", "runId", status=409)
                dt = value["dt"]
                if type(dt) not in (int, float):
                    raise ApiError("Expected finite atomic-unit time", "dt")
                try:
                    finite = math.isfinite(dt)
                except OverflowError:
                    finite = False
                if not finite:
                    raise ApiError("Expected finite atomic-unit time", "dt")
                result = self.exchange("advance " + repr(dt))
            else:
                raise ApiError("Unknown API route", status=404)
            result["runId"] = self.run_id
            return result
        finally:
            self.lock.release()


def create_server(worker, web_root, port=8000):
    web_root = Path(web_root).resolve()

    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(10)

        def valid_origin(self):
            hosts = {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}
            if self.headers.get("Host") not in hosts:
                raise ApiError("Use the local server address", status=403)
            origin = self.headers.get("Origin")
            if origin is not None and origin not in {"http://" + host for host in hosts}:
                raise ApiError("Requests must come from this local UI", status=403)
            if self.headers.get("Sec-Fetch-Site") not in (None, "none", "same-origin"):
                raise ApiError("Requests must come from this local UI", status=403)

        def send(self, status, content, mime):
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(content)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(content)

        def json(self, status, value):
            self.send(status, json.dumps(value, allow_nan=False, separators=(",", ":")).encode(), "application/json; charset=utf-8")

        def do_GET(self):
            try:
                self.valid_origin()
                path = urlsplit(self.path).path
                if path == "/api/catalog":
                    self.json(200, worker.request(path))
                    return
                routes = {"/": ("index.html", "text/html"), "/style.css": ("style.css", "text/css")}
                if path.startswith("/assets/") and path.count("/") == 2 and path.endswith(".js"):
                    routes[path] = ("dist/" + path.rsplit("/", 1)[1], "text/javascript")
                if path not in routes:
                    raise ApiError("Not found", status=404)
                name, mime = routes[path]
                file = (web_root / name).resolve()
                if not file.is_relative_to(web_root) or not file.is_file():
                    raise ApiError("UI assets are missing; run npm run build in web/", status=404)
                self.send(200, file.read_bytes(), mime + "; charset=utf-8")
            except ApiError as error:
                self.json(error.status, error.payload)

        def do_POST(self):
            try:
                self.valid_origin()
                path = urlsplit(self.path).path
                if path not in ("/api/sample", "/api/advance"):
                    raise ApiError("Unknown POST route", status=404)
                if self.headers.get("Content-Type", "").split(";", 1)[0].strip() != "application/json":
                    raise ApiError("Use application/json", status=415)
                try:
                    length = int(self.headers.get("Content-Length", "0"))
                except ValueError as error:
                    raise ApiError("Invalid Content-Length") from error
                if not 0 < length <= MAX_BODY:
                    raise ApiError("Request body must be between 1 and 8192 bytes", code="resource-limit", status=413)
                try:
                    value = json.loads(self.rfile.read(length), object_pairs_hook=unique_object, parse_constant=reject_constant)
                except (ValueError, UnicodeError, RecursionError) as error:
                    raise ApiError("Invalid JSON") from error
                self.json(200, worker.request(path, value))
            except ApiError as error:
                self.json(error.status, error.payload)

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    server.daemon_threads = True
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path(os.environ.get("QM_API_BINARY", ROOT / "build" / "atom_api")))
    parser.add_argument("--web-root", type=Path, default=ROOT / "web")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    if not args.binary.is_file():
        parser.error("Build atom_api first, or supply --binary")
    if not (args.web_root / "dist" / "main.js").is_file():
        parser.error("Build the UI first: npm --prefix web ci && npm --prefix web run build")
    worker = Worker(args.binary)
    server = create_server(worker, args.web_root, args.port)
    def stop(*_):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, stop)
    print(f"Orbital lab: http://127.0.0.1:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        with worker.lock:
            worker.close()


if __name__ == "__main__":
    main()
