"""pytest for collection (docs/COLLECTION.ja.md): a sketch's .py, run explicitly, builds the sketch, writes it to the
probe (pytest-embedded-arduino-cli: --profile, --port), and collects each of its cases into runs/.

    uv run pytest m5stack/u001-c/u001-c.py --profile esp32p4x --port <the probe's flash port> --bench p4x-grove

The files are named without test_ so that a bare `pytest` collects nothing: a collection writes the probe and the
repository's runs/, and is run on purpose.
"""

from pathlib import Path

import pytest
import yaml

import devicedata_collect as dc


def pytest_addoption(parser):
    group = parser.getgroup("devicedata", "EmbedBenchDeviceData collection")
    group.addoption("--bench", help="the bench the probe and its parts are on: collect/benches/<name>.yaml")
    group.addoption("--bus-hz", type=int, default=100_000, help="the bus speed the cases run at (default 100000)")
    group.addoption("--rate", type=int, default=2_000_000, help="logic samples per second (default 2000000)")
    group.addoption("--runs-out", type=Path, default=dc.REPO_ROOT / "runs",
                    help="where run directories go (default: the repository's runs/)")


@pytest.fixture(scope="session")
def settings(request) -> dc.Settings:
    name = request.config.getoption("--bench")
    if not name:
        pytest.exit("--bench <name> (collect/benches/<name>.yaml) is needed", returncode=4)
    bench = yaml.safe_load((dc.COLLECT_ROOT / "benches" / f"{name}.yaml").read_text(encoding="utf-8"))
    out = request.config.getoption("--runs-out")
    out.mkdir(parents=True, exist_ok=True)
    return dc.Settings(port=bench["probe"], lines=bench["lines"], bench=bench, hz=request.config.getoption("--bus-hz"),
                       rate=request.config.getoption("--rate"), out=out)


@pytest.fixture(scope="module")
def probe(settings):
    """A session on the probe, with its lease, for every case of the module (after the plugin wrote the sketch)."""
    hst = dc.open_probe(settings.port)
    yield hst
    dc.close_probe(hst)


@pytest.fixture(scope="session")
def profile(request):
    return request.config.getoption("--profile", default=None)
