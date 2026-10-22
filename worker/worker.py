"""DARVC worker placeholder.

This file intentionally does not run RVC yet. Integrate a pinned, licensed RVC
inference implementation here after confirming its CLI/API and dependencies.
The plugin UI currently does not launch this worker.
"""

import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python worker.py job.json", file=sys.stderr)
        return 2
    job_path = Path(sys.argv[1])
    if not job_path.is_file():
        print(f"Job file not found: {job_path}", file=sys.stderr)
        return 2
    job = json.loads(job_path.read_text(encoding="utf-8"))
    print(json.dumps({
        "job_id": job.get("job_id", ""),
        "state": "not_implemented",
        "message": "Connect a verified RVC inference backend before use."
    }))
    return 3


if __name__ == "__main__":
    raise SystemExit(main())
