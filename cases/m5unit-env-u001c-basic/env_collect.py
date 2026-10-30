"""Collect ENV III (U001-C) runs on the P4X at bench 3-3/3-4: one wireskein run directory per case.

    env_collect.py OUT_ROOT CASE[,CASE...] [--rate 2000000] [--samples N]

CASE: one of CASES below. Each run: run.json + c0001.wireskein (SDA, SCL on the
probe's clock, the case's ProbeLog lines placed on it)."""
import argparse
import struct
import time
from pathlib import Path

from oep_client import capture, core, link
from oep_client.probe_log import ProbeLog, rows
from wireskein.runlog import Recorder

PORT = "usb:80f1b2d719c5"
SDA, SCL = 14, 15
NAME = "io.github.example.library-test"
RUN, STATUS = 0x10, 0x20
CASES = {"scan": 1, "m5unit-env-u001c-basic": 2, "sht30-modes": 3, "qmp6988-modes": 4, "qmp6988-standby": 5,
         "qmp6988-measure-bit": 6, "sht30-stretch-wait": 7}
SAMPLES = {"scan": 400_000, "m5unit-env-u001c-basic": 2_000_000, "sht30-modes": 1_000_000, "qmp6988-modes": 1_200_000,
           "qmp6988-standby": 1_400_000, "qmp6988-measure-bit": 1_400_000, "sht30-stretch-wait": 600_000}


def run_case(hst, fn, case, timeout_s=20.0):
    hst.call(fn, RUN, bytes([case]))
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        state, which, result = struct.unpack("<BBi", hst.call(fn, STATUS, locked=False).payload[:6])
        if state == 2:
            assert which == case
            return result
        time.sleep(0.01)
    raise TimeoutError(f"case {case} still running")


def collect(out: Path, name: str, rate: int, samples: int):
    hst = link.open_host(PORT)
    hst.open(5000)
    try:
        lc, log = capture.LogicCapture(hst), ProbeLog(hst, NAME)
        core.plan_apply(hst, [(lc.fn, 0, SDA), (lc.fn, 1, SCL)])
        try:
            rec = Recorder(out, case=name, probe=PORT, pins={"SDA": SDA, "SCL": SCL})
            readings = [hst.clock_best()]
            log.clear()
            cfg = lc.configure(rate=rate, samples=samples, trigger=(capture.EDGE, 0, 1), pretrigger=2000)
            armed = rec.armed()
            lc.start()
            result = run_case(hst, log.fn, CASES[name])
            (seg,) = lc.wait(timeout=10.0)
            data = lc.read_segment(seg)
            lines = log.log()
            readings.append(hst.clock_best())
            rec.probe_lines(rows(lines, readings, PORT), lost=lines.lost + ([f"gap {lines.gap}"] if lines.gap else []),
                            probe=PORT, boot_id=readings[0].boot_id)
            rec.note(f"case result={result}")
            rec.capture(armed, cfg.rate, interleaved=data, names=["SDA", "SCL"], width=cfg.width,
                        positions=cfg.positions, n=seg.samples, start_ns=seg.start_ns,
                        start_uncertainty_ns=seg.start_uncertainty_ns, boot_id=readings[0].boot_id,
                        trigger_tick=seg.trigger_index, source=f"oep:{PORT}")
            rec.close()
            last = max((l.probe_ns for l in lines.lines), default=0)
            end_ns = seg.start_ns + seg.samples * 1e9 / float(cfg.rate)
            print(f"{name}: result={result} lines={len(lines.lines)} lost={lines.lost} samples={seg.samples} "
                  f"rate={float(cfg.rate):.0f} capture_end_ns={end_ns:.0f} last_line_ns={last} "
                  f"covered={last <= end_ns}")
        finally:
            core.plan_release(hst, [lc.fn])
    finally:
        hst.end()
        hst.link.close()


if __name__ == "__main__":
    a = argparse.ArgumentParser()
    a.add_argument("root")
    a.add_argument("cases")
    a.add_argument("--rate", type=int, default=2_000_000)
    a.add_argument("--samples", type=int)
    o = a.parse_args()
    for c in o.cases.split(","):
        collect(Path(o.root) / c, c, o.rate, o.samples or SAMPLES[c])
