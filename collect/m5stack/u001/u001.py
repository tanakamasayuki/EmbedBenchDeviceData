"""Collect the cases of M5Stack Unit ENV (U001: DHT12 at 0x5C, BMP280 at 0x76): every case whose collect.sketches names
m5stack/u001, by its number there (Cases.cpp).

    uv run pytest m5stack/u001/u001.py --profile esp32p4x --port <flash port> --bench p4x-grove-u001
"""

import pytest

import devicedata_collect as dc

SKETCH = "m5stack/u001"
INTERFACE = "io.github.tanakamasayuki.devicedata.u001-cases"
CASES = dc.cases_of(SKETCH)


@pytest.mark.parametrize("case", CASES, ids=[c["key"] for c in CASES])
def test_collect(case, probe, settings, profile):
    run = dc.collect_case(probe, case, settings, SKETCH, INTERFACE, profile)
    print(f"{run.name}")
    assert (run / "provenance.yaml").exists()
