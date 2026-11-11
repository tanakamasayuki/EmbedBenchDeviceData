"""Collect the cases of M5Stack Unit ENV IV (U001-D: SHT40 at 0x44, BMP280 at 0x76): every case whose collect.sketches names
m5stack/u001-d, by its number there (Cases.cpp).

    uv run pytest m5stack/u001-d/u001-d.py --profile esp32p4x --port <flash port> --bench p4x-grove-u001-d
"""

import pytest

import devicedata_collect as dc

SKETCH = "m5stack/u001-d"
INTERFACE = "io.github.tanakamasayuki.devicedata.u001d-cases"
CASES = dc.cases_of(SKETCH)


@pytest.mark.parametrize("case", CASES, ids=[c["key"] for c in CASES])
def test_collect(case, probe, settings, profile):
    run = dc.collect_case(probe, case, settings, SKETCH, INTERFACE, profile)
    print(f"{run.name}")
    assert (run / "provenance.yaml").exists()
