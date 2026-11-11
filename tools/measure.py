#!/usr/bin/env python3
"""Times measured from runs: what a case's event markers say happened, placed at the wire's transfers.

A case knows what it saw (a command accepted, a read refused, a new sample); the wire knows when. A case writes, inside
a phase, a third-level marker right after the call that saw it (docs/COLLECTION.ja.md §3):

    ### start <timing> [name=value ...]        a measurement of <timing> starts at the transfer just made, under
                                               these conditions (the run's bus speed is added as hz)
    ### before <timing> [at=...]               the transfer just made saw the state before <timing>'s event
    ### after <timing> [n=<k>] [at=...]        it saw the state after the event (the k-th one, for a series; 1 if left
                                               out): the first transfer to see it, so the same-shaped transfer before
                                               it (since the start) saw the state before

Each marker is placed at the last transfer that ended before it (the case writes it once the call returned). A time
is from the start of the start's transfer to the start of the observation's transfer, or with at=release to where
the transfer's clock stretch ended (the part let SCL go: the event itself, so it bounds both sides; a read the part
did not stretch is an after at its start). Nothing here
knows a part: a case's markers say what each transfer meant.

A measurement's window for event k is [the latest before, the earliest after]. A measurement with events 1..N is a
series t_k = first + (k - 1) * period: its period window is what every pair of events allows, and its first window
what every event allows with the period eliminated (both exact: the constraints are linear in two unknowns). Over
several measurements, a window is the intersection of theirs; an empty one says the time varies from one to the
next.

    python3 tools/measure.py [RUN ...] [--timing KEY] [--where NAME=VALUE ...] [--each]

With no runs named, every run under runs/ whose log carries event markers. knowledge/<chip>.yaml quotes these
windows (a window's `measured`), and tools/validate.py recomputes them.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent
MAX_LAG_US = 3000          # from a transfer's end to the marker the case wrote after it
EVENT = re.compile(r"^###\s+(start|before|after)\s+(\S+)(.*)$")


class MeasureError(ValueError):
    pass


@dataclass
class Transfer:
    index: int
    s: float               # probe time, us
    e: float
    shape: tuple
    release: float | None  # where its clock stretch ended


@dataclass
class Measurement:
    run: str
    timing: str
    where: dict
    start: Transfer
    marker_us: float
    before: dict = field(default_factory=dict)   # k -> latest time seen before event k
    after: dict = field(default_factory=dict)    # k -> earliest time seen after event k
    used: set = field(default_factory=set)       # transfers already placed

    def window(self, k: int = 1) -> tuple:
        return self.before.get(k), self.after.get(k)


def _params(text: str) -> dict:
    out = {}
    for token in text.split():
        if "=" not in token:
            raise MeasureError(f"not name=value: {token!r}")
        name, value = token.split("=", 1)
        out[name] = value
    return out


def load_transfers(run_dir: Path) -> list:
    prov = yaml.safe_load((run_dir / "provenance.yaml").read_text(encoding="utf-8"))
    capture = prov["captures"][0]
    base = capture["timebase"]
    num, den = base["tick_hz"]
    tick0, start_us = base.get("tick0", 0), base["start_ns"] / 1000

    def us(tick):
        return start_us + (tick - tick0) * den * 1_000_000 / num

    data = json.loads((run_dir / capture["decode"]).read_text(encoding="utf-8"))
    out, previous = [], None
    for index, t in enumerate(data.get("transactions", [])):
        shape = (t.get("addr"), t.get("rw"))
        if t.get("rw") == "read" and t.get("start_cond") == "Sr" and previous is not None \
                and previous.get("addr") == t.get("addr") and previous.get("rw") == "write":
            shape += (previous.get("bytes"),)   # a register read: the pointer written before it
        waits = t.get("scl_waits") or []
        release = us(waits[-1]["s"] + waits[-1]["ticks"]) if waits else None
        out.append(Transfer(index, us(t["s"]), us(t["e"]), shape, release))
        previous = t
    return out


def hz_of(run_dir: Path) -> str:
    prov = yaml.safe_load((run_dir / "provenance.yaml").read_text(encoding="utf-8"))
    return str(prov["bus"]["hz"])


def has_events(run_dir: Path) -> bool:
    try:
        log = json.loads((run_dir / "run.json").read_text(encoding="utf-8")).get("log", [])
    except (OSError, ValueError):
        return False
    return any(EVENT.match(line.get("text", "")) for line in log)


def measure_run(run_dir: Path) -> list:
    """Every measurement a run's event markers describe, in the order they started."""
    run = json.loads((run_dir / "run.json").read_text(encoding="utf-8"))
    transfers = None
    hz = hz_of(run_dir)
    open_ = {}
    out = []
    for line in run.get("log", []):
        text = line.get("text", "")
        if line.get("src") == "marker" and re.match(r"^#{1,2}(\s|$)", text):
            open_.clear()                  # a phase or a case ends what was measured in it
            continue
        m = EVENT.match(text)
        if not m:
            continue
        if transfers is None:
            transfers = load_transfers(run_dir)
        kind, timing, rest = m.groups()
        where = f"{run_dir.name}: {text!r}"
        if "probe_ns" not in line:
            raise MeasureError(f"{where}: no probe time")
        at_us = line["probe_ns"] / 1000
        done = [t for t in transfers if t.e <= at_us]
        if not done or at_us - done[-1].e > MAX_LAG_US:
            raise MeasureError(f"{where}: no transfer ended within {MAX_LAG_US} us before it")
        transfer = done[-1]
        params = _params(rest)
        if kind == "start":
            params["hz"] = hz
            measurement = Measurement(run_dir.name, timing, params, transfer, at_us)
            open_[timing] = measurement
            out.append(measurement)
            continue
        measurement = open_.get(timing)
        if measurement is None:
            raise MeasureError(f"{where}: no measurement of {timing} started in this phase")
        if transfer.index <= measurement.start.index or transfer.index in measurement.used:
            raise MeasureError(f"{where}: the transfer before it is not a new one (the call left nothing on the wire?)")
        measurement.used.add(transfer.index)
        unknown = set(params) - {"n", "at"}
        if unknown:
            raise MeasureError(f"{where}: unknown {sorted(unknown)}")
        k = int(params.get("n", 1))
        at = params.get("at", "start")
        if at == "release" and transfer.release is not None:
            time = transfer.release - measurement.start.s
            points = {"before": time, "after": time}
        elif at in ("start", "release"):   # release with no stretch: it was there when the read started
            time = transfer.s - measurement.start.s
            points = {kind: time}
            if kind == "after":            # the same-shaped transfer before it saw the state before
                earlier = [t for t in transfers
                           if measurement.start.index < t.index < transfer.index and t.shape == transfer.shape]
                if earlier:
                    points["before"] = earlier[-1].s - measurement.start.s
        else:
            raise MeasureError(f"{where}: at must be start or release")
        for side, value in points.items():
            table = getattr(measurement, side)
            pick = max if side == "before" else min
            table[k] = value if k not in table else pick(table[k], value)
    return out


def runs_named(names: list) -> list:
    if names:
        return [Path(n) if Path(n).is_dir() else REPO_ROOT / "runs" / n for n in names]
    return [p for p in sorted((REPO_ROOT / "runs").iterdir()) if p.is_dir() and has_events(p)]


def select(measurements: list, timing: str | None = None, where: dict | None = None) -> list:
    where = {k: str(v) for k, v in (where or {}).items()}
    return [m for m in measurements
            if (timing is None or m.timing == timing) and all(m.where.get(k) == v for k, v in where.items())]


def _r(value):
    return None if value is None else int(round(value))


def intersect(windows: list) -> tuple:
    lows = [lo for lo, _ in windows if lo is not None]
    highs = [hi for _, hi in windows if hi is not None]
    return (max(lows) if lows else None, min(highs) if highs else None)


def period(m: Measurement) -> tuple:
    """The period every pair of a series' events allows (None when it has fewer than two)."""
    ks = sorted(set(m.before) | set(m.after))
    low = high = None
    for i in ks:
        for j in ks:
            if j <= i:
                continue
            if m.before.get(j) is not None and m.after.get(i) is not None:
                v = (m.before[j] - m.after[i]) / (j - i)
                low = v if low is None else max(low, v)
            if m.after.get(j) is not None and m.before.get(i) is not None:
                v = (m.after[j] - m.before[i]) / (j - i)
                high = v if high is None else min(high, v)
    return low, high


def first(m: Measurement) -> tuple:
    """Event 1's time as the whole series allows it: lo_k <= first + (k - 1) * period <= hi_k for every k, the period
    eliminated pair by pair (with one event, its own window)."""
    low, high = m.window(1)
    ks = sorted(set(m.before) | set(m.after))
    for a in ks:
        for b in ks:
            if a < 2 or b < 2 or a == b or m.before.get(a) is None or m.after.get(b) is None:
                continue
            # (lo_a - first) / (a - 1) <= period <= (hi_b - first) / (b - 1)
            v = (m.before[a] * (b - 1) - m.after[b] * (a - 1)) / (b - a)
            if b > a:
                low = v if low is None else max(low, v)
            else:
                high = v if high is None else min(high, v)
    return low, high


def window_of(measurements: list, of: str = "first") -> tuple:
    """The window over measurements: of 'first' (event 1, with the rest of a series), 'period' (of a series), or an
    event number (that event's own window)."""
    if of == "period":
        return tuple(_r(v) for v in intersect([period(m) for m in measurements]))
    if of == "first":
        return tuple(_r(v) for v in intersect([first(m) for m in measurements]))
    k = int(of)
    return tuple(_r(v) for v in intersect([m.window(k) for m in measurements]))


def describe(measurements: list, each: bool = False) -> str:
    groups = {}
    for m in measurements:
        groups.setdefault((m.timing, tuple(sorted(m.where.items()))), []).append(m)
    lines = []
    for (timing, where), group in sorted(groups.items()):
        cond = " ".join(f"{k}={v}" for k, v in where)
        n = max(max(m.after or {0: 0}) for m in group)
        lo, hi = window_of(group)
        flag = "  (empty: varies)" if lo is not None and hi is not None and lo > hi else ""
        text = f"{timing} {cond}: {len(group)} measured, first [{lo}, {hi}]{flag}"
        if n > 1:
            plo, phi = window_of(group, "period")
            pflag = "  (empty)" if plo is not None and phi is not None and plo > phi else ""
            text += f", period [{plo}, {phi}]{pflag} over {n} events"
        lines.append(text)
        if each:
            for m in group:
                a, b = first(m)
                extra = ""
                if n > 1:
                    p = period(m)
                    extra = f" period [{_r(p[0])}, {_r(p[1])}]"
                lines.append(f"    {m.run} @{_r(m.start.s)}: [{_r(a)}, {_r(b)}]{extra}")
    return "\n".join(lines)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("runs", nargs="*")
    parser.add_argument("--timing")
    parser.add_argument("--where", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--each", action="store_true", help="every measurement, not only each group's window")
    args = parser.parse_args(argv)
    measurements = []
    try:
        for run_dir in runs_named(args.runs):
            measurements += measure_run(run_dir)
    except MeasureError as e:
        print(f"measure: {e}", file=sys.stderr)
        return 1
    print(describe(select(measurements, args.timing, _params(" ".join(args.where))), args.each))
    return 0


if __name__ == "__main__":
    sys.exit(main())
