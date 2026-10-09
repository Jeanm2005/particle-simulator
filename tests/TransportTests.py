import http.client
import importlib.util
import json
import math
from pathlib import Path
import sys
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("serve", ROOT / "tools" / "serve.py")
serve = importlib.util.module_from_spec(spec)
spec.loader.exec_module(serve)
BINARY = sys.argv.pop(1)


def request(**changes):
    value = {"contractVersion": 1, "model": "hydrogenic-orbital", "element": {"atomicNumber": 1},
             "state": {"n": 1, "l": 0, "m": 0}, "screening": "pure-z", "sampleCount": 50000, "seed": 42, "backend": "cpu"}
    value.update(changes)
    return value


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.worker = serve.Worker(BINARY)
        cls.server = serve.create_server(cls.worker, ROOT / "web", 0)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.worker.close()

    def call(self, route, body=None, raw=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=35)
        data = json.dumps(body) if raw is None and body is not None else raw
        method = "GET" if data is None else "POST"
        connection.request(method, route, data, headers or {"Content-Type": "application/json"})
        response = connection.getresponse()
        result = response.read()
        connection.close()
        return response.status, json.loads(result)

    def test_sampling_and_current(self):
        status, catalog = self.call("/api/catalog")
        self.assertEqual(status, 200)
        self.assertEqual(catalog["maxSamples"], 1000000)
        self.assertIn(79, [e["atomicNumber"] for e in catalog["elements"]])
        status, first = self.call("/api/sample", request())
        self.assertEqual(status, 200)
        self.assertEqual(first["actualSampleCount"], 50000)
        self.assertEqual(first["previewCount"], 20000)
        self.assertEqual(len(first["points"]), 20000)
        self.assertEqual(first["units"]["polarAxis"], "Y")
        self.assertAlmostEqual(first["meanRadiusA0"], 1.5, delta=0.025)
        _, same = self.call("/api/sample", request())
        self.assertEqual(first["points"], same["points"])
        status, stale = self.call("/api/advance", {"runId": first["runId"], "dt": 0.1})
        self.assertEqual(status, 409)
        self.assertEqual(stale["error"]["field"], "runId")
        status, stationary = self.call("/api/advance", {"runId": same["runId"], "dt": 0.1})
        self.assertEqual(status, 200)
        self.assertEqual(stationary["points"], same["points"])
        _, flowing = self.call("/api/sample", request(state={"n": 2, "l": 1, "m": -1}, sampleCount=500))
        status, advanced = self.call("/api/advance", {"runId": flowing["runId"], "dt": 0.1})
        self.assertEqual(status, 200)
        self.assertNotEqual(flowing["points"], advanced["points"])
        for before, after in zip(flowing["points"], advanced["points"]):
            self.assertEqual(before[1], after[1])
            self.assertAlmostEqual(math.hypot(*before), math.hypot(*after), places=12)
        self.assertEqual(flowing["meanRadiusA0"], advanced["meanRadiusA0"])
        self.assertEqual(advanced["timeAtomicUnits"], 0.1)
        _, gold = self.call("/api/sample", request(element={"atomicNumber": 79}, state={"n": 6, "l": 0, "m": 0}, screening="slater-neutral", sampleCount=100))
        self.assertAlmostEqual(gold["effectiveCharge"], 3.7, places=12)
        status, error = self.call("/api/sample", request(element={"atomicNumber": 120}, screening="slater-neutral"))
        self.assertEqual(status, 422)
        self.assertEqual(error["error"]["code"], "screening-unavailable")
        status, still_gold = self.call("/api/advance", {"runId": gold["runId"], "dt": 0.2})
        self.assertEqual(status, 200)
        self.assertEqual(still_gold["element"]["symbol"], "Au")

    def test_validation(self):
        for change, field, code in [
            ({"seed": -1}, "seed", "invalid-input"),
            ({"seed": 2**32}, "seed", "invalid-input"),
            ({"sampleCount": 1.5}, "sampleCount", "invalid-input"),
            ({"sampleCount": True}, "sampleCount", "invalid-input"),
            ({"element": {"atomicNumber": 2**31}}, "element.atomicNumber", "invalid-input"),
            ({"state": {"n": 1, "l": 1, "m": 0}}, "state.l", "invalid-input"),
            ({"state": {"n": 2, "l": 1, "m": -2}}, "state.m", "invalid-input"),
            ({"state": {"n": 8, "l": 0, "m": 0}}, "state.n", "invalid-input"),
            ({"model": "lattice-dynamics"}, "model", "unsupported-model"),
            ({"contractVersion": 2}, "contractVersion", "unsupported-version"),
            ({"sampleCount": 1000001}, "sampleCount", "resource-limit"),
            ({"screening": "nonsense"}, "screening", "invalid-input"),
        ]:
            with self.subTest(change=change):
                status, result = self.call("/api/sample", request(**change))
                self.assertGreaterEqual(status, 400)
                self.assertEqual(result["error"]["field"], field)
                self.assertEqual(result["error"]["code"], code)
        for value in (request(extra=1), request(element={"atomicNumber": 1, "config": "1s1"})):
            self.assertEqual(self.call("/api/sample", value)[0], 400)
        for raw in ('{"seed":1,"seed":2}', '{"seed":NaN}', 'null', '{broken', '[', '[' * 2000 + ']' * 2000):
            self.assertEqual(self.call("/api/sample", raw=raw)[0], 400)
        self.assertEqual(self.call("/api/sample", raw="x" * 8193)[0], 413)
        status, sampled = self.call("/api/sample", request(sampleCount=1))
        self.assertEqual(status, 200)
        for dt in (True, "0.1", 10**400):
            self.assertEqual(self.call("/api/advance", {"runId": sampled["runId"], "dt": dt})[0], 400)
        self.assertEqual(self.call("/api/advance", {"runId": sampled["runId"], "dt": 0})[0], 200)

    def test_worker_failure_and_busy(self):
        self.worker.lock.acquire()
        try:
            status, response = self.call("/api/catalog")
            self.assertEqual(status, 409)
            self.assertEqual(response["error"]["code"], "backend-busy")
        finally:
            self.worker.lock.release()
        _, sampled = self.call("/api/sample", request(sampleCount=10))
        self.worker.process.kill()
        self.worker.process.wait()
        status, response = self.call("/api/advance", {"runId": sampled["runId"], "dt": 0.1})
        self.assertEqual(status, 503)
        self.assertEqual(response["error"]["code"], "backend-unavailable")
        self.assertIsNone(self.worker.run_id)
        self.assertEqual(self.call("/api/catalog")[0], 200)
        self.assertEqual(self.call("/api/advance", {"runId": sampled["runId"], "dt": 0.1})[0], 409)
        timeout = self.worker.timeout
        self.worker.timeout = 0
        try:
            self.assertEqual(self.call("/api/catalog")[0], 503)
            self.assertIsNone(self.worker.process)
        finally:
            self.worker.timeout = timeout
        self.assertEqual(self.call("/api/sample", request(sampleCount=10))[0], 200)

    def test_origin_and_static_routes(self):
        self.assertEqual(self.call("/api/catalog", headers={"Host": "evil.example"})[0], 403)
        self.assertEqual(self.call("/api/catalog", headers={"Origin": "https://evil.example"})[0], 403)
        self.assertEqual(self.call("/api/catalog", headers={"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertEqual(self.call("/api/no-such-route")[0], 404)
        self.assertEqual(self.call("/api/catalog", {})[0], 404)
        self.assertEqual(self.call("/assets/../../data/elements.json")[0], 404)
        self.assertEqual(self.call("/api/sample", request(), headers={"Content-Type": "text/plain"})[0], 415)
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)
        connection.request("GET", "/")
        response = connection.getresponse()
        self.assertEqual(response.status, 200)
        self.assertIn(b"Orbital lab", response.read())
        self.assertIn("frame-ancestors 'none'", response.getheader("Content-Security-Policy"))
        connection.close()


if __name__ == "__main__":
    unittest.main()
