"""Collect the cases of M5Stack Unit ToF (U010: VL53L0X at 0x29): every case whose collect.sketches names
m5stack/u010, by its number there (Cases.cpp).

    uv run pytest m5stack/u010/u010.py --profile esp32p4x --port <flash port> --bench p4x-grove-u010
"""

import pytest

import devicedata_collect as dc

SKETCH = "m5stack/u010"
INTERFACE = "io.github.tanakamasayuki.devicedata.u010-cases"
CASES = dc.cases_of(SKETCH)


@pytest.mark.parametrize("case", CASES, ids=[c["key"] for c in CASES])
def test_collect(case, probe, settings, profile):
    run = dc.collect_case(probe, case, settings, SKETCH, INTERFACE, profile)
    print(f"{run.name}")
    assert (run / "provenance.yaml").exists()
