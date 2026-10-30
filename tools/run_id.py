#!/usr/bin/env python3
"""Name a run by what happened on the bus.

    <case>__i2c-<kHz>k__<hash16>

The hash is the first 16 hex digits of the sha256 of the decoded transfers,
times left out, so two runs that moved the same bytes in the same order get
the same name whenever they were taken. A run whose measured values differ
gets a different name; grouping those is the job of a mask, not of the id.

    python3 tools/run_id.py runs/<dir>      # print the name the run should have
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

import yaml

# The fields of a wireskein I2C transaction that say what happened, not when.
FIELDS = ("addr", "rw", "addr_ack", "bytes", "acks", "start_cond", "end_cond")


def transfers(decode: Path) -> list[dict]:
    data = json.loads(decode.read_text(encoding="utf-8"))
    return [{field: x.get(field) for field in FIELDS} for x in data["transactions"]]


def content_hash(decodes: list[Path]) -> str:
    sequence = [transfers(path) for path in decodes]
    text = json.dumps(sequence, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def run_id(provenance: dict, decodes: list[Path]) -> str:
    bus = provenance["bus"]
    if bus["kind"] != "i2c":
        raise ValueError("only i2c runs are named so far")
    hz = int(bus["hz"])
    if hz % 1000:
        raise ValueError(f"bus.hz {hz} is not a whole number of kHz")
    return f"{provenance['case']}__i2c-{hz // 1000}k__{content_hash(decodes)}"


def main() -> int:
    run_dir = Path(sys.argv[1])
    provenance = yaml.safe_load((run_dir / "provenance.yaml").read_text(encoding="utf-8"))
    decodes = [run_dir / entry["decode"] for entry in provenance["captures"]]
    print(run_id(provenance, decodes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
