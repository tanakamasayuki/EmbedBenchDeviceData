"""Collect the cases of M5Stack Unit ENV III (U001-C: SHT30 at 0x44, QMP6988 at 0x70): every case whose
collect.sketch is m5stack/u001-c, by its number in Cases.cpp.

    uv run pytest m5stack/u001-c/u001-c.py --profile esp32p4x --port <flash port> --bench p4x-grove [-k sht30]
"""

import pytest

import devicedata_collect as dc

SKETCH = "m5stack/u001-c"
INTERFACE = "io.github.tanakamasayuki.devicedata.env-cases"
CASES = dc.cases_of(SKETCH)


@pytest.mark.parametrize("case", CASES, ids=[c["key"] for c in CASES])
def test_collect(case, probe, settings, profile):
    run = dc.collect_case(probe, case, settings, INTERFACE, profile)
    print(f"{run.name}")
    assert (run / "provenance.yaml").exists()
