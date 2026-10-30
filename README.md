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
- **The OEP probe records** (capture plus case markers on the same clock);
  **wireskein analyses**. EmbedBench only reads `runs/`.
- Formats are bus-neutral (wireskein runs and analyses), so other uses are
  not ruled out.

## Layout

```text
chips/<chip>.yaml          catalog: what a chip is, where it answers on which bus
products/<product>.yaml    bill of materials for a vendor's whole line
libraries/<library>.yaml   libraries and the chips they drive
surveys/                   how a vendor line was enumerated
guides/chips/<chip>.md     human-readable device guides (Japanese)
cases/<case>/              collection cases: case.yaml and the OEP LibraryTest source
runs/<run-id>/             one recording: run.json, capture, decode, provenance.yaml
knowledge/<chip>.yaml      behaviour tables (registers, commands, timing), citing runs
tools/                     validate, run naming, index build
docs/DATA_MODEL.ja.md      the shape and rules of every file (authoritative, Japanese)
```

## Usage

```sh
python3 tools/validate.py            # check shapes and references
python3 tools/run_id.py runs/<dir>   # the name a run directory should have
python3 tools/build.py               # reverse indexes into build/ (not committed)
```

Requires Python 3.9+ and PyYAML.

## Origin

The catalog (`chips/`, `products/`, `libraries/`, `surveys/`, `guides/`) was
moved from [I2CDeviceDB](https://github.com/tanakamasayuki/I2CDeviceDB) at
commit 38f1d82, without its history.

## License

[MIT](LICENSE)
