"""DARVC conversion worker.

Runs one RVC (Retrieval-based Voice Conversion) job per invocation. The
plugin never runs inference on the audio thread; instead it writes a job
JSON file (see ``job.schema.example.json``), launches this script with a
Python interpreter on a background thread, and streams our stdout.

Protocol (one JSON object per stdout line)::

    {"job_id": "...", "state": "progress", "stage": "loading_model", "percent": 10, "message": "..."}
    {"job_id": "...", "state": "done", "percent": 100, "output": "/path/to/out.wav"}
    {"job_id": "...", "state": "error", "code": "engine_not_installed", "message": "..."}

``worker.py --check`` prints a single ``state=check`` object describing the
Python environment (interpreter version, rvc-python / torch availability,
CUDA/MPS support) so the plugin's Settings panel can guide installation.

The actual inference is delegated to the ``rvc-python`` package
(``pip install rvc-python``), which downloads its HuBERT/RMVPE base models
on first use. Only load model (.pth) files you trust: PyTorch checkpoints
can execute code when loaded.

Exit codes: 0 success/check, 2 usage or invalid job, 3 engine not
installed, 4 conversion failed.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

REQUIRED_FIELDS = ("input", "output", "model")
F0_METHODS = ("rmvpe", "harvest", "pm", "crepe")
MODEL_VERSIONS = ("v1", "v2")
DEVICES = ("auto", "cpu", "cuda", "mps")


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------

def emit(job_id: str, state: str, **fields: object) -> None:
    """Print one protocol JSON line, flushed so the plugin sees it live."""
    payload = {"job_id": job_id, "state": state, **fields}
    print(json.dumps(payload), flush=True)


def validate(job: object) -> str | None:
    """Return an error message if ``job`` does not match the job schema."""
    if not isinstance(job, dict):
        return "Job file must contain a JSON object."
    for field in REQUIRED_FIELDS:
        if not isinstance(job.get(field), str) or not job[field]:
            return f"Missing or invalid required field: {field!r}"
    for field in ("pitch", "index_rate", "protect"):
        value = job.get(field)
        if value is not None and not isinstance(value, (int, float)):
            return f"Field {field!r} must be a number."
    if "pitch" in job and not -24 <= job["pitch"] <= 24:
        return "Field 'pitch' must be between -24 and 24 semitones."
    for field in ("index_rate", "protect"):
        if field in job and not 0.0 <= job[field] <= 1.0:
            return f"Field {field!r} must be between 0.0 and 1.0."
    if job.get("f0_method", "rmvpe") not in F0_METHODS:
        return f"Field 'f0_method' must be one of {F0_METHODS}."
    if job.get("model_version", "v2") not in MODEL_VERSIONS:
        return "Field 'model_version' must be 'v1' or 'v2'."
    if job.get("device", "auto") not in DEVICES:
        return f"Field 'device' must be one of {DEVICES}."
    return None


def find_sibling_index(model_path: Path) -> str:
    """Locate an .index file matching a model, next to the .pth file.

    RVC training tools (incl. Applio) export ``<name>.pth`` together with an
    index whose filename usually *contains* the model name rather than being
    exactly ``<name>.index``, so fall back to any sibling index.
    """
    exact = model_path.with_suffix(".index")
    if exact.is_file():
        return str(exact)
    siblings = sorted(model_path.parent.glob("*.index"))
    for candidate in siblings:
        if model_path.stem in candidate.stem:
            return str(candidate)
    return str(siblings[0]) if siblings else ""


def resolve_device(requested: str, torch_module: object | None) -> str:
    """Map the plugin's device choice onto rvc-python's device strings."""
    if requested == "cpu":
        return "cpu"
    if requested == "cuda":
        return "cuda:0"
    if requested == "mps":
        return "mps"
    # auto: prefer CUDA, otherwise CPU (rvc-python's MPS support is spotty).
    try:
        if torch_module is not None and torch_module.cuda.is_available():  # type: ignore[attr-defined]
            return "cuda:0"
    except Exception:
        pass
    return "cpu"


# --------------------------------------------------------------------------
# Engine check (Settings panel)
# --------------------------------------------------------------------------

def engine_check() -> dict:
    info: dict[str, object] = {
        "python_version": "%d.%d.%d" % sys.version_info[:3],
        "python_executable": sys.executable,
        "rvc_python": False,
        "torch": False,
        "cuda": False,
        "mps": False,
    }
    try:
        import importlib.metadata as metadata

        info["rvc_python_version"] = metadata.version("rvc-python")
        info["rvc_python"] = True
    except Exception:
        pass
    try:
        import torch

        info["torch"] = True
        info["torch_version"] = getattr(torch, "__version__", "?")
        try:
            info["cuda"] = bool(torch.cuda.is_available())
        except Exception:
            pass
        try:
            info["mps"] = bool(torch.backends.mps.is_available())
        except Exception:
            pass
    except Exception:
        pass
    info["engine_ready"] = bool(info["rvc_python"] and info["torch"])
    return info


# --------------------------------------------------------------------------
# Conversion
# --------------------------------------------------------------------------

def run_job(job: dict) -> int:
    job_id = str(job.get("job_id", ""))

    error = validate(job)
    if error is not None:
        emit(job_id, "error", code="invalid_job", message=error)
        return 2

    input_path = Path(job["input"])
    model_path = Path(job["model"])
    output_path = Path(job["output"])
    for label, path in (("Input audio", input_path), ("Model", model_path)):
        if not path.is_file():
            emit(job_id, "error", code="invalid_job",
                 message=f"{label} file not found: {path}")
            return 2

    index_path = job.get("index") or find_sibling_index(model_path)
    if index_path and not Path(index_path).is_file():
        index_path = ""

    emit(job_id, "progress", stage="loading_engine", percent=5,
         message="Loading RVC engine...")

    try:
        import torch  # noqa: F401  (needed for device detection & rvc-python)
        from rvc_python.infer import RVCInference
    except ImportError as exc:
        emit(job_id, "error", code="engine_not_installed",
             message=("RVC engine is not installed in this Python environment "
                      f"({exc}). Open Settings in DARVC and use "
                      "'Install / Repair Engine', or run: pip install rvc-python"))
        return 3

    device = resolve_device(str(job.get("device", "auto")), torch)
    models_dir = str(job.get("models_dir") or model_path.parent)

    try:
        emit(job_id, "progress", stage="loading_model", percent=15,
             message=f"Loading model on {device} "
                     "(first run downloads HuBERT/RMVPE base models)...")
        rvc = RVCInference(models_dir=models_dir, device=device)
        rvc.load_model(str(model_path),
                       version=str(job.get("model_version", "v2")),
                       index_path=index_path)
        # rvc-python 0.1.x only recognises these exact keyword names.
        rvc.set_params(
            f0up_key=int(job.get("pitch", 0)),
            f0method=str(job.get("f0_method", "rmvpe")),
            index_rate=float(job.get("index_rate", 0.75)),
            protect=float(job.get("protect", 0.33)),
            filter_radius=3,
            resample_sr=0,
            rms_mix_rate=0.25,
        )

        emit(job_id, "progress", stage="converting", percent=45,
             message="Converting voice...")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        rvc.infer_file(str(input_path), str(output_path))
        rvc.unload_model()
    except Exception as exc:  # surface any engine failure to the UI
        output_path.unlink(missing_ok=True)
        emit(job_id, "error", code="conversion_failed",
             message=f"Conversion failed: {exc}")
        return 4

    if not output_path.is_file() or output_path.stat().st_size == 0:
        emit(job_id, "error", code="conversion_failed",
             message="Conversion finished but no output file was produced.")
        return 4

    emit(job_id, "done", percent=100, output=str(output_path),
         message="Conversion complete.")
    return 0


# --------------------------------------------------------------------------
# Entry point
# --------------------------------------------------------------------------

def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--check":
        emit("", "check", **engine_check())
        return 0
    if len(sys.argv) != 2:
        print("Usage: python worker.py job.json | --check", file=sys.stderr)
        return 2

    job_path = Path(sys.argv[1])
    if not job_path.is_file():
        print(f"Job file not found: {job_path}", file=sys.stderr)
        return 2
    try:
        job = json.loads(job_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        emit("", "error", code="invalid_job",
             message=f"Could not read job file: {exc}")
        return 2

    return run_job(job)


if __name__ == "__main__":
    raise SystemExit(main())
