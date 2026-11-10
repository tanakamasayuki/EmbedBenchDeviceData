# EmbedBenchDeviceData

[日本語](README.ja.md)

**Recorded behaviour of real devices**, for building and checking the device
models of [EmbedBench](https://github.com/tanakamasayuki/EmbedBench).

EmbedBench tests libraries and applications on a host against device models.
How right a model is depends on what the real part does; this repository keeps
those records (runs) and the catalog, cases and behaviour tables needed to read
them.

- **A recording is preparation for testing, not a test.** A real part is driven
  once, its lines are captured and analysed, and the result becomes material
  for a host model and the test that checks the model.
- **This repository collects.** It keeps the cases (`cases/`) and, under
  `collect/`, the sketches written to the probe and the pytest that collects;
  the probe is driven with OEP's libraries (OpenEmbeddedProbe,
  oep-client-python), and runs are recorded and decoded with wireskein. See
  docs/COLLECTION.ja.md.
- **The raw capture and the markers are the record.** A decode is the analysis
  of its day and can always be redone from the raw capture.
- **Other repositories only read `runs/` and `knowledge/`.** EmbedBench uses
  them for model material and comparison (`skein2trace.py`,
  `knowledge_table.py`); wireskein for its device declarations
  (`knowledge_decl.py`) and for scoring its decoder.
- Formats are bus-neutral (wireskein runs and analyses), so other uses are
  not ruled out.

## Layout

```text
chips/<chip>.yaml          catalog: what a chip is, where it answers on which bus
products/<product>.yaml    bill of materials for a vendor's whole line
libraries/<library>.yaml   libraries and the chips they drive
surveys/                   how a vendor line was enumerated
guides/chips/<chip>.md     human-readable device guides (Japanese)
cases/<case>/              collection cases: case.yaml (intent, targets, phases, which firmware case)
collect/                   collection: one sketch per product (<vendor>/<sku>/), the common firmware and Python, benches, pytest
runs/<run-id>/             one recording: run.json, the raw capture, decode, provenance.yaml
knowledge/<chip>.yaml      behaviour tables (registers, commands, timing), citing runs
tools/                     validate, run naming, index build (converters to EmbedBench tables and wireskein declarations live in those repositories)
docs/DATA_MODEL.ja.md      the shape and rules of every file (authoritative, Japanese)
docs/COLLECTION.ja.md      how runs are collected (Japanese)
```

## Usage

```sh
python3 tools/validate.py            # check shapes and references
python3 tools/run_id.py runs/<dir>   # the name a run directory should have
python3 tools/build.py               # reverse indexes into build/ (not committed)
```

Requires Python 3.9+ and PyYAML. Collection also needs arduino-cli and uv (`uv sync` in `collect/`).

## Origin

The catalog (`chips/`, `products/`, `libraries/`, `surveys/`, `guides/`) was
moved from [I2CDeviceDB](https://github.com/tanakamasayuki/I2CDeviceDB) at
commit 38f1d82, without its history. Chip addresses were regrouped into
per-bus `interfaces`, and product parts carry their `bus`. I2CDeviceDB's
`profiles/` and `observations/` were not moved: behaviour tables live in
`knowledge/` here, citing runs, and the guides' references to `profiles/`
and the like are pinned links to that commit.

## License

[MIT](LICENSE)
