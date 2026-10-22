"""Protocol tests for worker.py.

The real RVC engine (torch + rvc-python + a trained .pth model) is far too
heavy for CI, so these tests inject a fake ``rvc_python``/``torch`` pair on
PYTHONPATH. The fake engine copies the input file to the output and records
every call, which verifies the full plugin<->worker glue: job validation,
device/parameter mapping, .index discovery and the JSON-lines protocol.

Run: python -m unittest worker/test_worker.py  (stdlib only)
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

WORKER = Path(__file__).with_name("worker.py")

FAKE_TORCH = '''
__version__ = "0.0-fake"
class _Cuda:
    @staticmethod
    def is_available(): return False
class _Mps:
    @staticmethod
    def is_available(): return False
class _Backends: mps = _Mps()
cuda = _Cuda()
backends = _Backends()
'''

FAKE_INFER = '''
import json, os, shutil

class RVCInference:
    def __init__(self, models_dir="", device="cpu"):
        self.device = device
        self.models_dir = models_dir
        self.calls = {"init": {"models_dir": models_dir, "device": device}}

    def _save(self):
        with open(os.environ["FAKE_RVC_CALLS"], "w") as fh:
            json.dump(self.calls, fh)

    def load_model(self, model_path_or_name, version="v2", index_path=""):
        self.calls["load_model"] = {"model": model_path_or_name,
                                    "version": version, "index": index_path}
        self._save()

    def set_params(self, **kwargs):
        self.calls["set_params"] = kwargs
        self._save()

    def infer_file(self, input_path, output_path):
        shutil.copyfile(input_path, output_path)
        self.calls["infer_file"] = {"input": input_path, "output": output_path}
        self._save()
        return output_path

    def unload_model(self):
        self.calls["unloaded"] = True
        self._save()
'''


def run_worker(args: list[str], env_extra: dict[str, str] | None = None,
               fake_engine_dir: Path | None = None) -> tuple[int, list[dict], str]:
    env = dict(os.environ)
    if fake_engine_dir is not None:
        env["PYTHONPATH"] = str(fake_engine_dir) + os.pathsep + env.get("PYTHONPATH", "")
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run([sys.executable, str(WORKER), *args],
                          capture_output=True, text=True, env=env, timeout=120)
    events = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            events.append(json.loads(line))
    return proc.returncode, events, proc.stderr


class WorkerTestCase(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

        # Fake engine packages.
        self.fake_dir = self.root / "fake"
        (self.fake_dir / "rvc_python").mkdir(parents=True)
        (self.fake_dir / "rvc_python" / "__init__.py").write_text("")
        (self.fake_dir / "rvc_python" / "infer.py").write_text(FAKE_INFER)
        (self.fake_dir / "torch.py").write_text(FAKE_TORCH)
        self.calls_file = self.root / "calls.json"

        # Job inputs.
        (self.root / "in.wav").write_bytes(b"RIFF-fake-audio")
        self.model_dir = self.root / "models" / "singer"
        self.model_dir.mkdir(parents=True)
        (self.model_dir / "singer.pth").write_bytes(b"fake-checkpoint")
        (self.model_dir / "added_singer_v2.index").write_bytes(b"fake-index")

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def write_job(self, **overrides: object) -> Path:
        job = {
            "job_id": "test-1",
            "input": str(self.root / "in.wav"),
            "output": str(self.root / "out" / "Converted.wav"),
            "model": str(self.model_dir / "singer.pth"),
            "pitch": 2,
            "f0_method": "rmvpe",
            "index_rate": 0.75,
            "protect": 0.33,
            "device": "auto",
            "model_version": "v2",
        }
        job.update(overrides)
        path = self.root / "job.json"
        path.write_text(json.dumps(job))
        return path

    # -- tests ------------------------------------------------------------

    def test_check_reports_environment(self) -> None:
        code, events, _ = run_worker(["--check"], fake_engine_dir=self.fake_dir)
        self.assertEqual(code, 0)
        self.assertEqual(len(events), 1)
        check = events[0]
        self.assertEqual(check["state"], "check")
        self.assertTrue(check["torch"])
        self.assertFalse(check["cuda"])
        self.assertIn("python_version", check)

    def test_check_without_engine(self) -> None:
        empty = self.root / "empty"
        empty.mkdir()
        code, events, _ = run_worker(["--check"], env_extra={"PYTHONPATH": str(empty)})
        self.assertEqual(code, 0)
        self.assertEqual(events[0]["state"], "check")

    def test_full_conversion_with_fake_engine(self) -> None:
        job = self.write_job()
        code, events, stderr = run_worker(
            [str(job)],
            env_extra={"FAKE_RVC_CALLS": str(self.calls_file)},
            fake_engine_dir=self.fake_dir)
        self.assertEqual(code, 0, stderr)
        states = [e["state"] for e in events]
        self.assertEqual(states, ["progress", "progress", "progress", "done"])
        done = events[-1]
        self.assertEqual(done["job_id"], "test-1")
        self.assertTrue(Path(done["output"]).is_file())

        calls = json.loads(self.calls_file.read_text())
        # auto + no CUDA -> cpu; sibling .index found by model-name match.
        self.assertEqual(calls["init"]["device"], "cpu")
        self.assertTrue(calls["load_model"]["index"].endswith("added_singer_v2.index"))
        self.assertEqual(calls["load_model"]["version"], "v2")
        # Parameter names must match rvc-python 0.1.x exactly.
        self.assertEqual(calls["set_params"]["f0up_key"], 2)
        self.assertEqual(calls["set_params"]["f0method"], "rmvpe")
        self.assertAlmostEqual(calls["set_params"]["index_rate"], 0.75)
        self.assertAlmostEqual(calls["set_params"]["protect"], 0.33)
        self.assertTrue(calls["unloaded"])

    def test_cuda_device_mapping(self) -> None:
        job = self.write_job(device="cuda")
        code, _, stderr = run_worker(
            [str(job)], env_extra={"FAKE_RVC_CALLS": str(self.calls_file)},
            fake_engine_dir=self.fake_dir)
        self.assertEqual(code, 0, stderr)
        calls = json.loads(self.calls_file.read_text())
        self.assertEqual(calls["init"]["device"], "cuda:0")

    def test_missing_engine_reports_error_not_traceback(self) -> None:
        # torch.py absent (real torch is not installed in this environment)
        only_rvc = self.root / "only_rvc"
        (only_rvc / "rvc_python").mkdir(parents=True)
        (only_rvc / "rvc_python" / "__init__.py").write_text("")
        (only_rvc / "rvc_python" / "infer.py").write_text(FAKE_INFER)
        job = self.write_job()
        code, events, stderr = run_worker([str(job)], fake_engine_dir=only_rvc)
        if code == 0:
            self.skipTest("a real torch installation is present; cannot simulate")
        self.assertEqual(code, 3)
        self.assertEqual(events[-1]["state"], "error")
        self.assertEqual(events[-1]["code"], "engine_not_installed")
        self.assertNotIn("Traceback", stderr)

    def test_invalid_jobs(self) -> None:
        job = self.write_job()
        path = self.root / "bad.json"
        path.write_text('{"oops": ')
        code, events, _ = run_worker([str(path)])
        self.assertEqual(code, 2)
        self.assertEqual(events[-1]["code"], "invalid_job")

        missing = self.write_job(model=str(self.root / "nope.pth"))
        code, events, _ = run_worker([str(missing)], fake_engine_dir=self.fake_dir)
        self.assertEqual(code, 2)
        self.assertIn("not found", events[-1]["message"])

        bad_field = self.write_job(f0_method="telepathy")
        code, events, _ = run_worker([str(bad_field)], fake_engine_dir=self.fake_dir)
        self.assertEqual(code, 2)
        self.assertIn("f0_method", events[-1]["message"])


if __name__ == "__main__":
    unittest.main()
