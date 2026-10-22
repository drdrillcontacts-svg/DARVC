"""DARVC worker placeholder.

This file intentionally does not run RVC yet. Integrate a pinned, licensed RVC
inference implementation here after confirming its CLI/API and dependencies.
The plugin UI currently does not launch this worker.

Protocol: reads a single job JSON file (see job.schema.example.json), prints
one JSON status object to stdout, and exits with:

* 0 - success (unused until a backend is connected)
* 2 - usage error / unreadable or invalid job file
* 3 - backend not implemented yet
"""

import json
import sys
from pathlib import Path

REQUIRED_FIELDS = ("input", "output", "model")
NUMERIC_FIELDS = ("pitch", "index_rate", "protect")


def emit(job: dict, state: str, message: str) -> None:
    print(json.dumps({
        "job_id": job.get("job_id", "") if isinstance(job, dict) else "",
        "state": state,
        "message": message,
    }))


def validate(job: object) -> str | None:
    """Return an error message if ``job`` does not match the job schema."""
    if not isinstance(job, dict):
        return "Job file must contain a JSON object."
    for field in REQUIRED_FIELDS:
        if not isinstance(job.get(field), str) or not job[field]:
            return f"Missing or invalid required field: {field!r}"
    for field in NUMERIC_FIELDS:
        value = job.get(field)
        if value is not None and not isinstance(value, (int, float)):
            return f"Field {field!r} must be a number."
    for field in ("pitch",):
        if field in job and not -24 <= job[field] <= 24:
            return "Field 'pitch' must be between -24 and 24 semitones."
    for field in ("index_rate", "protect"):
        if field in job and not 0.0 <= job[field] <= 1.0:
            return f"Field {field!r} must be between 0.0 and 1.0."
    return None


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python worker.py job.json", file=sys.stderr)
        return 2
    job_path = Path(sys.argv[1])
    if not job_path.is_file():
        print(f"Job file not found: {job_path}", file=sys.stderr)
        return 2

    try:
        job = json.loads(job_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        emit({}, "error", f"Could not read job file: {exc}")
        return 2

    error = validate(job)
    if error is not None:
        emit(job, "error", error)
        return 2

    emit(job, "not_implemented",
         "Connect a verified RVC inference backend before use.")
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
