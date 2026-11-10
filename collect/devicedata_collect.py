"""The host side of a collection, shared by every sketch under collect/: run one case on the probe, capture its lines,
and write the run directory (docs/COLLECTION.ja.md).

A sketch's firmware runs on the probe itself and serves the ProbeCases interface (collect/libraries/ProbeCases): op
0x10 run, 0x14 configure, 0x20 status, and the case's log of markers and results on the probe's clock. On a session
the caller holds (the probe's lease), collect_case:

  1. configures the bus speed and clears the log;
  2. streams the case's lines (oep_client.acquire, mode "stream") while it starts the case and waits for it to end;
  3. writes run.json (the case's lines) and c0001.wireskein with wireskein's Recorder;
  4. decodes the capture with wireskein (c0001.analysis.json, c0001.i2c.json) and writes provenance.yaml;
  5. renames the directory to its run id (tools/run_id.py).

A capture or a log that lost anything fails the case, and nothing is left under a run's name.
"""

from __future__ import annotations

import datetime
import json
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from dataclasses import dataclass
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
COLLECT_ROOT = REPO_ROOT / "collect"
sys.path.insert(0, str(REPO_ROOT / "tools"))
import run_id as run_id_tool  # noqa: E402

OP_RUN, OP_CONFIGURE, OP_STATUS = 0x10, 0x14, 0x20
OWNER = "embedbench-device-data.collect"

# How each bus kind is captured and decoded: the roles a case's collect.lines names, and wireskein's decode spec.
DECODE = {
    "i2c": lambda lines: {"buses": [{"protocol": "i2c", "pins": {"sda": lines["SDA"], "scl": lines["SCL"]},
                                     "params": {}}], "lines": [], "depth": "device"},
}


@dataclass
class Settings:
    port: str                 # the probe's OEP address
    lines: dict[str, str]     # role -> the line the probe captures (its channel name, GPIO14)
    bench: dict               # collect/benches/<bench>.yaml
    hz: int                   # the bus speed the case runs at
    rate: int                 # logic samples per second
    out: Path                 # where run directories go


def load_case(key: str) -> dict:
    return yaml.safe_load((REPO_ROOT / "cases" / key / "case.yaml").read_text(encoding="utf-8"))


def cases_of(sketch: str) -> list[dict]:
    """The cases whose collect.sketch is this sketch (vendor/sku), in number order."""
    out = []
    for path in sorted((REPO_ROOT / "cases").glob("*/case.yaml")):
        case = yaml.safe_load(path.read_text(encoding="utf-8"))
        if (case.get("collect") or {}).get("sketch") == sketch:
            out.append(case)
    return sorted(out, key=lambda c: c["collect"]["number"])


def bus_kind(case: dict) -> str:
    kinds = {t["bus"] for t in case["targets"]} or {"i2c"}
    if len(kinds) != 1:
        raise ValueError(f"{case['key']}: one bus kind a case, found {sorted(kinds)}")
    (kind,) = kinds
    if kind not in DECODE:
        raise NotImplementedError(f"{case['key']}: collecting {kind} is not written yet")
    return kind


# ---- provenance ------------------------------------------------------------------------------------------------------
def git_commit(path: Path) -> str:
    try:
        head = subprocess.run(["git", "-C", str(path), "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(path), "status", "--porcelain", "--", "."], capture_output=True,
                               text=True, check=True).stdout.strip()
        return head + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def profile_versions(sketch_dir: Path, profile: str | None) -> dict[str, str]:
    """Platform and library versions from the sketch's sketch.yaml (the profile used, else its default)."""
    data = yaml.safe_load((sketch_dir / "sketch.yaml").read_text(encoding="utf-8"))
    chosen = data["profiles"][profile or data["default_profile"]]
    versions = {}
    for entry in chosen.get("platforms", []) + chosen.get("libraries", []):
        text = entry if isinstance(entry, str) else entry.get("platform", "")
        m = re.match(r"^(.*?) \((.+)\)$", text)
        if m:
            versions[m.group(1)] = m.group(2)
    return versions


def provenance(case: dict, s: Settings, kind: str, sketch_dir: Path, profile: str | None,
               captures: list[dict]) -> dict:
    import oep_client
    import wireskein
    versions = profile_versions(sketch_dir, profile)
    benches = {t["chip"]: t.get("bench") for t in s.bench.get("targets", [])}
    targets = []
    for target in case["targets"]:
        entry = dict(target)
        if benches.get(target["chip"]):
            entry["bench"] = dict(benches[target["chip"]])
        targets.append(entry)
    dut = s.bench["dut"]
    out = {
        "schema_version": 1,
        "case": case["key"],
        "date": datetime.date.today().isoformat(),
        "targets": targets,
        "bus": {"kind": kind, "hz": s.hz, "pins": dict(s.bench.get("pins", {})),
                "pullups": s.bench.get("pullups", "unknown")},
        "dut": {"board": dut["board"], "id": s.port, "bench": dict(dut["bench"]),
                "core": {"name": "arduino-esp32", "version": versions.get("esp32:esp32", "unknown")},
                "controller": dut.get("controller", "unknown")},
        "probe": {"id": s.port, "bench": dict(dut["bench"]),
                  "firmware": f"EmbedBenchDeviceData collect/{case['collect']['sketch']}",
                  "version": f"{git_commit(sketch_dir)} (OpenEmbeddedProbe {versions.get('OpenEmbeddedProbe', 'unknown')})"},
        "wireskein": {"version": wireskein.__version__},
        "captures": captures,
        "notes": f"collected with oep-client-python {oep_client.__version__}; the case ran on the probe itself",
    }
    if case.get("library"):
        library = yaml.safe_load((REPO_ROOT / "libraries" / f"{case['library']}.yaml").read_text(encoding="utf-8"))
        out["library"] = {"key": case["library"], "version": versions.get(library["name"], "unknown")}
    return out


# ---- one case --------------------------------------------------------------------------------------------------------
def collect_case(hst, case: dict, s: Settings, interface: str, profile: str | None = None) -> Path:
    """Run `case` on the probe held by `hst` and write its run directory; returns the directory."""
    from oep_client import acquire
    from oep_client.probe_log import ProbeLog, rows
    from wireskein import decode
    from wireskein.runlog import Recorder
    from wireskein.sources.oep import capture_of

    spec = case["collect"]
    kind = bus_kind(case)
    sketch_dir = COLLECT_ROOT / spec["sketch"]
    roles = spec["lines"]
    log = ProbeLog(hst, interface)
    hst.call(log.fn, OP_CONFIGURE, struct.pack("<I", s.hz))
    log.clear()

    speed = f"{s.hz // 1000}k"
    work = s.out / f"{case['key']}__{kind}-{speed}__pending"
    if work.exists():
        shutil.rmtree(work)
    rec = Recorder(work, case=case["key"], probe=s.port, pins={role: s.lines[role] for role in roles})
    readings = [hst.clock_best()]
    result = {}

    def steps(wait):
        wait(0.05)   # the stream runs before the case's first transfer
        hst.call(log.fn, OP_RUN, bytes([spec["number"]]))
        deadline = time.monotonic() + spec.get("timeout_s", 60)
        while time.monotonic() < deadline:
            state, which, value = struct.unpack("<BBi", hst.call(log.fn, OP_STATUS, locked=False).payload[:6])
            if state == 2:
                if which != spec["number"]:
                    raise RuntimeError(f"{case['key']}: the probe finished case {which}, not {spec['number']}")
                result["value"] = value
                wait(0.05)
                return
            wait(0.01)
        raise TimeoutError(f"{case['key']}: still running after {spec.get('timeout_s', 60)} s")

    armed = rec.armed()
    res = acquire.capture(hst, acquire.Request([(role, s.lines[role]) for role in roles], s.rate, 0,
                                               mode="stream", during=steps))
    readings.append(hst.clock_best())
    if res.lost:
        raise RuntimeError(f"{case['key']}: the capture lost data: {res.lost}")
    case_log = log.log()
    if case_log.lost or case_log.gap:
        raise RuntimeError(f"{case['key']}: the case's log lost lines: {case_log.lost} gap={case_log.gap}")

    rec.add(armed, capture_of(res))
    rec.probe_lines(rows(case_log, readings, s.port), lost=[], probe=s.port, boot_id=readings[0].boot_id)
    rec.note(f"case result={result.get('value')}")
    rec.close()

    analysis = decode.decode_file(work / "c0001.wireskein", DECODE[kind]({r: r for r in roles}))
    (work / "c0001.analysis.json").write_text(json.dumps(analysis, indent=1) + "\n", encoding="utf-8")
    transactions = [t for part in analysis["parts"] for bus in part["buses"] for t in bus["result"]["transactions"]]
    (work / f"c0001.{kind}.json").write_text(json.dumps({"transactions": transactions}, indent=1) + "\n",
                                            encoding="utf-8")

    with zipfile.ZipFile(work / "c0001.wireskein") as z:   # the same facts tools/validate.py checks
        head = json.loads(z.read("capture.json"))
    part = (head.get("parts") or [{}])[0]
    timebase = {"tick_hz": list(head["tick_hz"]), "tick0": part.get("tick0", 0),
                "start_ns": (part.get("meta") or {}).get("start_ns")}
    captures = [{"file": "c0001.wireskein", "decode": f"c0001.{kind}.json", "timebase": timebase}]
    prov = provenance(case, s, kind, sketch_dir, profile, captures)
    (work / "provenance.yaml").write_text(yaml.safe_dump(prov, sort_keys=False, allow_unicode=True), encoding="utf-8")

    final = s.out / run_id_tool.run_id(prov, [work / captures[0]["decode"]])
    if final.exists():
        shutil.rmtree(work)
        return final
    work.rename(final)
    return final


def open_probe(port: str, wait_s: float = 30.0):
    """A session on the probe with its lease, waiting for it to come up (after a flash it enumerates again)."""
    from oep_client import core, link
    deadline = time.monotonic() + wait_s
    while True:
        try:
            hst = link.open_host(port)
            break
        except Exception:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.5)
    core.take(hst, 10_000, owner=OWNER)
    return hst


def close_probe(hst) -> None:
    try:
        hst.end()
    finally:
        hst.link.close()
