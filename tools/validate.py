#!/usr/bin/env python3
"""Validate the data files against docs/DATA_MODEL.ja.md.

Everything here is hand-edited YAML plus the files a run leaves behind, so
nothing but a checker keeps the references between them true. This tool
loads every file, applies the per-kind rules, resolves every cross
reference, and reports every problem it finds rather than stopping at the
first one.

    python3 tools/validate.py          # exit 1 when anything is wrong
"""

from __future__ import annotations

import json
import re
import sys
import zipfile
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_id as run_id_tool  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent

SCHEMA_VERSION = 1
SLUG = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
BUS_ADDRESS = re.compile(r"0x[0-9a-f]{2}")
RESERVED_BUS_ADDRESSES = set(range(0x00, 0x08)) | set(range(0x78, 0x80))
DATASHEET_SOURCES = {"manufacturer", "vendor", "reseller"}
DATASHEET_KINDS = {"datasheet", "protocol", "manual", "product-page"}
# Recorded when the document itself states no revision or no date, so
# "checked, none given" is distinguishable from "nobody has looked yet".
UNSTATED = "unstated"
# How a host talks to a product, in the order an entry must list them.
#   i2c    an addressable target on the bus
#   spi    a target selected by a chip-select line
#   uart   asynchronous serial, including its RS485 / Modbus / DMX flavours
#   can    a CAN_TX / CAN_RX pair needing a CAN controller
#   pdm    a PDM audio clock / data pair needing an I2S or PDM peripheral
#   gpio   a digital level, a pulse, or a single timed data line
#   analog a voltage read by (or driven into) the host ADC / DAC
#   host   the unit is the controller; the port carries its own master-side pins
#   none   no data interface at all: passive adapters, power, prototyping boards
INTERFACE_KINDS = ("i2c", "spi", "uart", "can", "pdm", "gpio", "analog", "host", "none")
# Buses a chip can be catalogued on, with the field that says where on the
# bus it answers. Only i2c has entries so far; the others are here so the
# first SPI or UART part has a shape to fill instead of inventing one.
CHIP_BUSES = {"i2c": "addresses", "spi": "modes", "uart": "bauds"}
CASE_KINDS = {"library", "characterization"}
RUN_FILE_RE = re.compile(r"[A-Za-z0-9._-]+")
UNKNOWN = "unknown"


class Report:
    """Collects problems keyed by the file they belong to."""

    def __init__(self) -> None:
        self.problems: list[tuple[str, str]] = []

    def add(self, path: Path | str, message: str) -> None:
        if isinstance(path, Path):
            try:
                path = str(path.relative_to(REPO_ROOT))
            except ValueError:
                path = str(path)
        self.problems.append((path, message))

    def __bool__(self) -> bool:
        return bool(self.problems)


class StrictLoader(yaml.SafeLoader):
    """SafeLoader that refuses duplicate mapping keys.

    PyYAML silently keeps the last value, which turns a botched edit into
    data loss that every other check happily passes.
    """


def _no_duplicate_keys(loader, node, deep=False):
    mapping = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in mapping:
            raise yaml.constructor.ConstructorError(
                None, None, f"duplicate key {key!r}", key_node.start_mark)
        mapping[key] = loader.construct_object(value_node, deep=deep)
    return mapping


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _no_duplicate_keys)


def load_yaml(path: Path, report: Report) -> dict | None:
    try:
        data = yaml.load(path.read_text(encoding="utf-8"), Loader=StrictLoader)
    except yaml.YAMLError as exc:
        where = getattr(exc, "problem_mark", None)
        line = f"line {where.line + 1}: " if where else ""
        report.add(path, f"{line}{getattr(exc, 'problem', exc)}")
        return None
    if not isinstance(data, dict):
        report.add(path, "top level must be a mapping")
        return None
    return data


def load_masters(directory: str, report: Report) -> dict[Path, dict]:
    """Load every master under ``directory``; a broken file is reported and skipped."""
    masters = {}
    for path in sorted((REPO_ROOT / directory).glob("*.yaml")):
        data = load_yaml(path, report)
        if data is not None:
            masters[path] = data
    return masters


def check_fields(report, path, data, required, optional, may_be_empty=frozenset(), where=""):
    """Check the field vocabulary of one mapping.

    ``may_be_empty`` covers fields that must be present as a deliberate
    statement even when there is nothing to list yet — an empty
    ``datasheets`` says "no publishable document was found", which is
    different from omitting the field and never having looked.
    """
    prefix = f"{where}: " if where else ""
    for field in sorted(required - set(data)):
        report.add(path, f"{prefix}missing required field: {field}")
    for field in sorted(set(data) - required - optional):
        report.add(path, f"{prefix}unknown field: {field}")
    for field in sorted((set(data) & required) - may_be_empty):
        if data[field] in (None, "", [], {}):
            report.add(path, f"{prefix}required field is empty: {field}")


BLOCK_SCALAR_RE = re.compile(r"^(?P<indent>\s*)(?:- )?[\w-]+:\s*[|>][-+]?\d*\s*$")


def check_no_comments(report: Report, path: Path) -> None:
    """Reject YAML comments, but not a '#' that is part of the data.

    Text inside a block scalar is content (notes quote things like
    '#define'), so those spans are skipped.
    """
    block_indent = None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.lstrip()
        if block_indent is not None:
            if not stripped or len(line) - len(stripped) > block_indent:
                continue
            block_indent = None
        match = BLOCK_SCALAR_RE.match(line)
        if match:
            block_indent = len(match.group("indent"))
            continue
        if stripped.startswith("#"):
            report.add(path, f"line {number}: data files carry no comments; use notes: instead")


def check_common(report, path, data, key_field="key"):
    check_no_comments(report, path)
    if "schema_version" in data and data["schema_version"] != SCHEMA_VERSION:
        report.add(path, f"schema_version must be {SCHEMA_VERSION}, found {data.get('schema_version')!r}")
    if data.get(key_field) != path.stem:
        report.add(path, f"file stem must equal {key_field}: {data.get(key_field)!r}")


def slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def check_datasheets(report: Report, path: Path, data: dict) -> None:
    for index, sheet in enumerate(data.get("datasheets") or []):
        where = f"datasheets[{index}]"
        allowed = {"id", "source", "kind", "vendor", "url", "revision", "date", "note"}
        kind = sheet.get("kind", "datasheet")
        if kind not in DATASHEET_KINDS:
            report.add(path, f"{where}: kind must be one of {sorted(DATASHEET_KINDS)}, found {kind!r}")
        for field in sorted(set(sheet) - allowed):
            report.add(path, f"{where}: unknown field: {field}")
        source = sheet.get("source")
        if source not in DATASHEET_SOURCES:
            report.add(path, f"{where}: source must be one of {sorted(DATASHEET_SOURCES)}, found {source!r}")
        if source == "vendor" and not sheet.get("vendor"):
            report.add(path, f"{where}: source vendor requires a vendor field")
        if source != "vendor" and "vendor" in sheet:
            report.add(path, f"{where}: vendor field only applies to source vendor")
        url = sheet.get("url", "")
        if not str(url).startswith("https://"):
            report.add(path, f"{where}: url must be https, found {url!r}")
        date = sheet.get("date")
        if date is not None and date != UNSTATED and not re.fullmatch(r"\d{4}(-\d{2}){0,2}", str(date)):
            report.add(path, f"{where}: date must be YYYY, YYYY-MM, YYYY-MM-DD or {UNSTATED}, found {date!r}")
        if sheet.get("revision") == UNSTATED and sheet.get("date") == UNSTATED:
            report.add(path, f"{where}: a document with neither revision nor date states nothing; omit date")


def check_bus_address(report, path, where, addr) -> None:
    text = str(addr)
    if not BUS_ADDRESS.fullmatch(text):
        report.add(path, f"{where}: bus address must match 0x<lowercase hex pair>, found {addr!r}")
        return
    if int(text, 16) in RESERVED_BUS_ADDRESSES:
        report.add(path, f"{where}: {text} is an I2C reserved address")


# ---------------------------------------------------------------- catalog


def chip_addresses(chip: dict, bus: str = "i2c") -> set[str]:
    for interface in chip.get("interfaces") or []:
        if interface.get("bus") == bus:
            return {str(entry.get("addr")) for entry in interface.get("addresses") or []}
    return set()


def validate_chips(report: Report, chips: dict[Path, dict]) -> None:
    for path, data in chips.items():
        check_common(report, path, data)
        check_fields(report, path, data,
                     required={"schema_version", "key", "name", "manufacturer", "tags",
                               "interfaces", "datasheets"},
                     optional={"notes"},
                     may_be_empty={"datasheets"})
        if not data.get("datasheets") and not data.get("notes"):
            report.add(path, "a chip with no datasheet must explain the gap in notes")
        if not SLUG.fullmatch(str(data.get("key", ""))):
            report.add(path, f"chip key must be a part-number slug, found {data.get('key')!r}")
        buses = []
        for index, interface in enumerate(data.get("interfaces") or []):
            where = f"interfaces[{index}]"
            bus = interface.get("bus")
            if bus not in CHIP_BUSES:
                report.add(path, f"{where}: bus must be one of {sorted(CHIP_BUSES)}, found {bus!r}")
                continue
            buses.append(bus)
            check_fields(report, path, interface, required={"bus", CHIP_BUSES[bus]},
                         optional={"note"}, where=where)
            if bus == "i2c":
                seen = set()
                for number, entry in enumerate(interface.get("addresses") or []):
                    at = f"{where}.addresses[{number}]"
                    for field in sorted(set(entry) - {"addr", "note"}):
                        report.add(path, f"{at}: unknown field: {field}")
                    check_bus_address(report, path, at, entry.get("addr"))
                    if str(entry.get("addr")) in seen:
                        report.add(path, f"{at}: duplicate address {entry.get('addr')}")
                    seen.add(str(entry.get("addr")))
        if len(set(buses)) != len(buses):
            report.add(path, f"one interfaces entry per bus, found {buses}")
        check_datasheets(report, path, data)


def check_product_interfaces(report: Report, path: Path, data: dict) -> None:
    """``products/`` covers a whole vendor line, so every entry says how its
    unit is spoken to; a unit off every catalogued bus has to say what it
    is instead."""
    interfaces = data.get("interfaces")
    if not isinstance(interfaces, list) or not interfaces:
        report.add(path, "interfaces must be a non-empty list")
        return
    unknown = [kind for kind in interfaces if kind not in INTERFACE_KINDS]
    if unknown:
        report.add(path, f"unknown interface kind(s) {unknown}, expected one of {list(INTERFACE_KINDS)}")
        return
    if len(set(interfaces)) != len(interfaces):
        report.add(path, f"duplicate interface kind in {interfaces}")
    canonical = [kind for kind in INTERFACE_KINDS if kind in interfaces]
    if interfaces != canonical:
        report.add(path, f"interfaces must be listed in the order {canonical}, found {interfaces}")
    if "none" in interfaces and len(interfaces) > 1:
        report.add(path, f"'none' means there is no data interface, so it cannot be combined: {interfaces}")
    components = data.get("components") or []
    if "i2c" in interfaces and not components:
        report.add(path, "interfaces says i2c, so at least one component must be listed")
    for index, component in enumerate(components):
        if component.get("bus") not in interfaces:
            report.add(path, f"components[{index}]: bus {component.get('bus')!r} is not in interfaces")
    if not components and not str(data.get("notes") or "").strip():
        report.add(path, "a product with no catalogued component must say in notes what it is instead")


def validate_products(report, products, chips_by_key) -> None:
    for path, data in products.items():
        check_common(report, path, data)
        check_fields(report, path, data,
                     required={"schema_version", "key", "name", "vendor", "sku",
                               "connector", "url", "interfaces", "components"},
                     optional={"notes"},
                     may_be_empty={"components"})
        expected = f"{slug(str(data.get('vendor', '')))}-{slug(str(data.get('sku', '')))}"
        if data.get("key") != expected:
            report.add(path, f"product key must be slug(vendor)-slug(sku) = {expected!r}")
        if not str(data.get("url", "")).startswith("https://"):
            report.add(path, f"url must be https, found {data.get('url')!r}")
        check_product_interfaces(report, path, data)
        seen = set()
        for index, component in enumerate(data.get("components") or []):
            where = f"components[{index}]"
            bus = component.get("bus")
            if bus != "i2c":
                report.add(path, f"{where}: only i2c components are defined so far, found {bus!r}")
                continue
            check_fields(report, path, component, required={"chip", "bus", "addr"},
                         optional=set(), where=where)
            check_bus_address(report, path, where, component.get("addr"))
            addr = str(component.get("addr"))
            if addr in seen:
                report.add(path, f"{where}: duplicate address {addr} on one bus")
            seen.add(addr)
            chip = chips_by_key.get(component.get("chip"))
            if chip is None:
                report.add(path, f"{where}: unknown chip: {component.get('chip')!r}")
                continue
            catalog = chip_addresses(chip)
            if addr not in catalog:
                report.add(path, f"{where}: {addr} is not in chips/{component.get('chip')}.yaml "
                                 f"i2c addresses {sorted(catalog)}")


def validate_libraries(report, libraries, chips_by_key) -> None:
    for path, data in libraries.items():
        check_common(report, path, data)
        check_fields(report, path, data,
                     required={"schema_version", "key", "name", "repo", "supports"},
                     optional={"notes"})
        repo = str(data.get("repo", ""))
        match = re.fullmatch(r"https://github\.com/([^/]+)/([^/]+)", repo)
        if match is None:
            report.add(path, f"repo must be https://github.com/<owner>/<repo>, found {repo!r}")
        else:
            owner, name = slug(match.group(1)), slug(match.group(2))
            expected = name if name.startswith(f"{owner}-") else f"{owner}-{name}"
            if data.get("key") != expected:
                report.add(path, f"library key must be derived from repo = {expected!r}")
        supports = data.get("supports") or {}
        for field in sorted(set(supports) - {"chips"}):
            report.add(path, f"supports: unknown field: {field}")
        chips = supports.get("chips") or []
        if not chips:
            report.add(path, "supports.chips lists the chips this library drives")
        for chip_key in chips:
            if chip_key not in chips_by_key:
                report.add(path, f"supports.chips: unknown chip: {chip_key!r}")
        if len(set(chips)) != len(chips):
            report.add(path, "supports.chips has duplicates")


def validate_surveys(report, surveys, products) -> None:
    """A survey pins down the boundary of a vendor line: an ``out_of_line``
    entry must fail ``sku_pattern``, a ``pending`` one must match it, and
    neither may already be a product."""
    registered = {str(data.get("sku", "")).upper(): data.get("key") for data in products.values()}
    for path, data in surveys.items():
        check_common(report, path, data)
        check_fields(report, path, data,
                     required={"schema_version", "key", "vendor", "line", "sku_pattern",
                               "method", "source", "out_of_line"},
                     optional={"pending", "notes"})
        try:
            pattern = re.compile(str(data.get("sku_pattern", "")))
        except re.error as exc:
            report.add(path, f"sku_pattern is not a regular expression: {exc}")
            pattern = None
        seen = set()
        for section in ("out_of_line", "pending"):
            for index, entry in enumerate(data.get(section) or []):
                where = f"{section}[{index}]"
                for field in sorted(set(entry) - {"sku", "page", "reason"}):
                    report.add(path, f"{where}: unknown field: {field}")
                sku = str(entry.get("sku", ""))
                if not sku:
                    report.add(path, f"{where}: missing sku")
                if not entry.get("reason"):
                    report.add(path, f"{where}: missing reason")
                if sku.upper() in registered:
                    report.add(path, f"{where}: {sku} is registered as {registered[sku.upper()]}")
                if pattern is not None and sku:
                    matches = bool(pattern.match(sku.upper()))
                    if section == "out_of_line" and matches:
                        report.add(path, f"{where}: {sku} matches sku_pattern, so it belongs in products/")
                    if section == "pending" and not matches:
                        report.add(path, f"{where}: {sku} does not match sku_pattern; record it under out_of_line")
                if sku in seen:
                    report.add(path, f"{where}: duplicate sku {sku}")
                seen.add(sku)


def check_orphans(report, chips, products) -> None:
    referenced = {c.get("chip") for data in products.values() for c in data.get("components") or []}
    for path, data in chips.items():
        if data.get("key") not in referenced:
            report.add(path, "chip is referenced by no product; a catalog entry needs a carrier")


# ---------------------------------------------------------------- cases and runs


def check_targets(report, path, data, chips_by_key, products_by_key, where="targets"):
    """An empty list is allowed only with notes: a run against an address
    nothing answers is still evidence of what the controller does."""
    targets = data.get("targets")
    if not isinstance(targets, list):
        report.add(path, f"{where} must be a list")
        return
    if not targets and not str(data.get("notes") or "").strip():
        report.add(path, f"{where} is empty; say in notes what the run talks to instead")
    for index, target in enumerate(targets):
        at = f"{where}[{index}]"
        check_fields(report, path, target, required={"chip", "bus", "addr"},
                     optional={"product", "bench"}, where=at)
        check_bench(report, path, f"{at}.bench", target.get("bench"))
        chip = chips_by_key.get(target.get("chip"))
        if chip is None:
            report.add(path, f"{at}: unknown chip: {target.get('chip')!r}")
        elif target.get("bus") == "i2c" and str(target.get("addr")) not in chip_addresses(chip):
            report.add(path, f"{at}: {target.get('addr')} is not an i2c address of {target.get('chip')}")
        product = target.get("product")
        if product is not None:
            data = products_by_key.get(product)
            if data is None:
                report.add(path, f"{at}: unknown product: {product!r}")
            elif not any(c.get("chip") == target.get("chip") and str(c.get("addr")) == str(target.get("addr"))
                         for c in data.get("components") or []):
                report.add(path, f"{at}: {product} does not carry {target.get('chip')} at {target.get('addr')}")


def validate_cases(report, chips_by_key, products_by_key, libraries_by_key) -> dict[str, dict]:
    cases = {}
    for directory in sorted(p for p in (REPO_ROOT / "cases").iterdir() if p.is_dir()):
        path = directory / "case.yaml"
        if not path.exists():
            report.add(directory, "a case directory needs case.yaml")
            continue
        data = load_yaml(path, report)
        if data is None:
            continue
        check_no_comments(report, path)
        if data.get("key") != directory.name:
            report.add(path, f"key must equal the directory name: {data.get('key')!r}")
        if not SLUG.fullmatch(str(data.get("key", ""))):
            report.add(path, f"case key must be [a-z0-9-], found {data.get('key')!r}")
        check_fields(report, path, data,
                     required={"schema_version", "key", "kind", "intent", "targets"},
                     optional={"library", "phases", "source", "notes"},
                     may_be_empty={"targets"})
        if data.get("schema_version") != SCHEMA_VERSION:
            report.add(path, f"schema_version must be {SCHEMA_VERSION}")
        kind = data.get("kind")
        if kind not in CASE_KINDS:
            report.add(path, f"kind must be one of {sorted(CASE_KINDS)}, found {kind!r}")
        if kind == "library" and data.get("library") not in libraries_by_key:
            report.add(path, f"a library case names a known library, found {data.get('library')!r}")
        if kind == "characterization" and "library" in data:
            report.add(path, "a characterization case drives the part directly; it names no library")
        check_targets(report, path, data, chips_by_key, products_by_key)
        for index, phase in enumerate(data.get("phases") or []):
            check_fields(report, path, phase, required={"name", "intent"}, optional=set(),
                         where=f"phases[{index}]")
        for source in data.get("source") or []:
            if not (directory / source).exists():
                report.add(path, f"source: {source} is not in the case directory")
        cases[data.get("key")] = data
    return cases


def check_version(report, path, where, value):
    if value in (None, ""):
        report.add(path, f"{where}: state the version, or {UNKNOWN}")


def check_bench(report, path, where, bench):
    """The bench's name for the unit, and where it stood on the day of the run.

    Names belong to units, not positions (the oep-bench ledger), so a unit
    keeps its name when it moves; the position is a record of that day.
    """
    if bench is None:
        return
    check_fields(report, path, bench, required={"name", "position"}, optional=set(), where=where)
    if not re.fullmatch(r"[a-z0-9-]+", str(bench.get("name"))):
        report.add(path, f"{where}.name must be [a-z0-9-], found {bench.get('name')!r}")


def validate_provenance(report, path, data, cases, chips_by_key, products_by_key, libraries_by_key):
    check_no_comments(report, path)
    check_fields(report, path, data,
                 required={"schema_version", "case", "date", "targets", "bus", "dut", "probe",
                           "wireskein", "captures"},
                 optional={"library", "notes"},
                 may_be_empty={"targets"})
    if data.get("schema_version") != SCHEMA_VERSION:
        report.add(path, f"schema_version must be {SCHEMA_VERSION}")
    case = cases.get(data.get("case"))
    if case is None:
        report.add(path, f"unknown case: {data.get('case')!r}")
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", str(data.get("date"))):
        report.add(path, f"date must be YYYY-MM-DD, found {data.get('date')!r}")
    check_targets(report, path, data, chips_by_key, products_by_key)
    bus = data.get("bus") or {}
    check_fields(report, path, bus, required={"kind", "hz"}, optional={"pins", "pullups"}, where="bus")
    if bus.get("kind") != "i2c":
        report.add(path, f"bus.kind: only i2c runs are defined so far, found {bus.get('kind')!r}")
    if not isinstance(bus.get("hz"), int) or bus.get("hz", 0) <= 0:
        report.add(path, f"bus.hz must be a positive integer, found {bus.get('hz')!r}")
    dut = data.get("dut") or {}
    check_fields(report, path, dut, required={"board", "core", "controller"}, optional={"id", "bench"}, where="dut")
    check_bench(report, path, "dut.bench", dut.get("bench"))
    core = dut.get("core") or {}
    check_fields(report, path, core, required={"name", "version"}, optional=set(), where="dut.core")
    check_version(report, path, "dut.core.version", core.get("version"))
    probe = data.get("probe") or {}
    check_fields(report, path, probe, required={"id", "firmware", "version"}, optional={"bench"},
                 where="probe")
    check_bench(report, path, "probe.bench", probe.get("bench"))
    if dut.get("id") is not None and dut.get("id") == probe.get("id") \
            and (dut.get("bench") or {}).get("name") != (probe.get("bench") or {}).get("name"):
        report.add(path, "dut and probe are one unit (same id) but have different bench names")
    check_version(report, path, "probe.version", probe.get("version"))
    skein = data.get("wireskein") or {}
    check_fields(report, path, skein, required={"version"}, optional=set(), where="wireskein")
    check_version(report, path, "wireskein.version", skein.get("version"))
    library = data.get("library")
    if case is not None and case.get("kind") == "library":
        if library is None:
            report.add(path, "a run of a library case records the library and its version")
        elif library.get("key") != case.get("library"):
            report.add(path, f"library.key must be the case's library {case.get('library')!r}")
    if library is not None:
        check_fields(report, path, library, required={"key", "version"}, optional={"commit"}, where="library")
        if library.get("key") not in libraries_by_key:
            report.add(path, f"library.key: unknown library: {library.get('key')!r}")
        check_version(report, path, "library.version", library.get("version"))


def validate_capture_entry(report, path, run_dir, index, entry, run_json_files):
    where = f"captures[{index}]"
    check_fields(report, path, entry, required={"file", "decode", "timebase"}, optional={"stored", "notes"},
                 where=where)
    name = str(entry.get("file", ""))
    if not RUN_FILE_RE.fullmatch(name):
        report.add(path, f"{where}: file must be a plain name in the run directory, found {name!r}")
        return
    if run_json_files is not None and name not in run_json_files:
        report.add(path, f"{where}: {name} is not one of run.json's captures {sorted(run_json_files)}")
    stored = entry.get("stored", True)
    if stored is not True and not str(entry.get("notes") or "").strip():
        report.add(path, f"{where}: a capture kept outside the repository says where in notes")
    if stored is True and not (run_dir / name).exists():
        report.add(path, f"{where}: {name} is missing (set stored: false and say where it is)")
    timebase = entry.get("timebase") or {}
    check_fields(report, path, timebase, required={"tick_hz", "tick0", "start_ns"}, optional=set(),
                 where=f"{where}.timebase")
    if stored is True and (run_dir / name).exists():
        try:
            with zipfile.ZipFile(run_dir / name) as z:
                cap = json.loads(z.read("capture.json"))
            part = (cap.get("parts") or [{}])[0]
            actual = {"tick_hz": list(cap.get("tick_hz") or []), "tick0": part.get("tick0", 0),
                      "start_ns": (part.get("meta") or {}).get("start_ns")}
            given = {"tick_hz": list(timebase.get("tick_hz") or []), "tick0": timebase.get("tick0"),
                     "start_ns": timebase.get("start_ns")}
            if actual != given:
                report.add(path, f"{where}.timebase {given} does not match {name} {actual}")
        except (zipfile.BadZipFile, KeyError, json.JSONDecodeError) as exc:
            report.add(path, f"{where}: {name} is not a readable wireskein capture: {exc}")
    decode = str(entry.get("decode", ""))
    if not (run_dir / decode).exists():
        report.add(path, f"{where}: decode {decode} is missing")
        return None
    return run_dir / decode


def validate_runs(report, cases, chips_by_key, products_by_key, libraries_by_key) -> None:
    for run_dir in sorted(p for p in (REPO_ROOT / "runs").iterdir() if p.is_dir()):
        path = run_dir / "provenance.yaml"
        if not path.exists():
            report.add(run_dir, "a run directory needs provenance.yaml")
            continue
        data = load_yaml(path, report)
        if data is None:
            continue
        validate_provenance(report, path, data, cases, chips_by_key, products_by_key, libraries_by_key)
        run_json_files = None
        if not (run_dir / "run.json").exists():
            report.add(run_dir, "a run directory needs run.json")
        else:
            try:
                run = json.loads((run_dir / "run.json").read_text(encoding="utf-8"))
                run_json_files = {c.get("file") for c in run.get("captures") or []}
                if not str(run.get("format", "")).startswith("wireskein-run/"):
                    report.add(run_dir / "run.json", f"format must be wireskein-run/<n>, found {run.get('format')!r}")
            except json.JSONDecodeError as exc:
                report.add(run_dir / "run.json", f"not JSON: {exc}")
        decodes = []
        for index, entry in enumerate(data.get("captures") or []):
            decode = validate_capture_entry(report, path, run_dir, index, entry, run_json_files)
            if decode is not None:
                decodes.append(decode)
        if not data.get("captures"):
            continue
        try:
            expected = run_id_tool.run_id(data, decodes)
        except (ValueError, KeyError, json.JSONDecodeError) as exc:
            report.add(path, f"cannot compute the run id: {exc}")
            continue
        if run_dir.name.endswith("__pending"):
            report.add(run_dir, f"provisional name; rename it to {expected}")
        elif run_dir.name != expected:
            report.add(run_dir, f"run directory must be named {expected}")


# ---------------------------------------------------------------- knowledge

# Evidence outside runs/: the I2CDeviceDB pilot (by observation id) and the
# datasheet. Anything else must name a run directory.
EXTERNAL_EVIDENCE = re.compile(r"(i2cdevicedb-pilot:[a-z0-9-]+|datasheet)")


def evidence_of(item):
    value = item.get("evidence")
    return value if isinstance(value, list) else [value] if value else []


def validate_knowledge(report, chips_by_key) -> None:
    runs = {p.name for p in (REPO_ROOT / "runs").iterdir() if p.is_dir()}
    for path in sorted((REPO_ROOT / "knowledge").glob("*.yaml")):
        data = load_yaml(path, report)
        if data is None:
            continue
        check_no_comments(report, path)
        check_fields(report, path, data,
                     required={"schema_version", "chip", "bus", "registers"},
                     optional={"addressing", "timing", "behaviours", "notes"})
        if data.get("chip") != path.stem:
            report.add(path, f"file stem must equal chip: {data.get('chip')!r}")
        if data.get("chip") not in chips_by_key:
            report.add(path, f"unknown chip: {data.get('chip')!r}")
        items = [("addressing", data.get("addressing") or {})]
        seen = set()
        for index, reg in enumerate(data.get("registers") or []):
            where = f"registers[{index}]"
            items.append((where, reg))
            check_fields(report, path, reg, required={"addr", "width", "access", "reset"},
                         optional={"name", "volatile", "per_specimen", "evidence", "notes"}, where=where)
            if not re.fullmatch(r"0x[0-9A-F]{2}", str(reg.get("addr"))):
                report.add(path, f"{where}: addr must be 0x<uppercase hex pair>, found {reg.get('addr')!r}")
            if reg.get("access") not in ("r", "w", "rw"):
                report.add(path, f"{where}: access must be r, w or rw")
            try:
                if len(bytes.fromhex(str(reg.get("reset")))) != reg.get("width"):
                    report.add(path, f"{where}: reset must be width ({reg.get('width')}) bytes of hex")
            except ValueError:
                report.add(path, f"{where}: reset is not hex: {reg.get('reset')!r}")
            if reg.get("addr") in seen:
                report.add(path, f"{where}: duplicate register {reg.get('addr')}")
            seen.add(reg.get("addr"))
        for index, item in enumerate(data.get("timing") or []):
            items.append((f"timing[{index}]", item))
            for number, window in enumerate(item.get("windows") or []):
                items.append((f"timing[{index}].windows[{number}]", window))
        for index, item in enumerate(data.get("behaviours") or []):
            items.append((f"behaviours[{index}]", item))
        for where, item in items:
            for ref in evidence_of(item):
                if ref not in runs and not EXTERNAL_EVIDENCE.fullmatch(str(ref)):
                    report.add(path, f"{where}: evidence {ref!r} is neither a run nor a known source")


# ---------------------------------------------------------------- guides


GUIDE_LINK = re.compile(r"\]\((?!https?://|#)([^)#\s]+)")


def validate_guides(report, chips_by_key) -> None:
    directory = REPO_ROOT / "guides" / "chips"
    for path in sorted(directory.glob("*.md")):
        if path.name == "INDEX.ja.md":
            continue
        if path.stem not in chips_by_key:
            report.add(path, f"guide for an unknown chip: {path.stem!r}")
        for target in GUIDE_LINK.findall(path.read_text(encoding="utf-8")):
            if not (path.parent / target).resolve().exists():
                report.add(path, f"broken link: {target}")


def validate() -> Report:
    report = Report()
    chips = load_masters("chips", report)
    products = load_masters("products", report)
    libraries = load_masters("libraries", report)
    surveys = load_masters("surveys", report)
    for name, masters in {"chips": chips, "products": products,
                          "libraries": libraries, "surveys": surveys}.items():
        seen = {}
        for path, data in masters.items():
            if data.get("key") in seen:
                report.add(path, f"duplicate {name} key {data.get('key')!r}, also in {seen[data.get('key')].name}")
            seen[data.get("key")] = path
    chips_by_key = {data.get("key"): data for data in chips.values()}
    products_by_key = {data.get("key"): data for data in products.values()}
    libraries_by_key = {data.get("key"): data for data in libraries.values()}
    validate_chips(report, chips)
    validate_products(report, products, chips_by_key)
    validate_libraries(report, libraries, chips_by_key)
    validate_surveys(report, surveys, products)
    check_orphans(report, chips, products)
    cases = validate_cases(report, chips_by_key, products_by_key, libraries_by_key)
    validate_runs(report, cases, chips_by_key, products_by_key, libraries_by_key)
    validate_knowledge(report, chips_by_key)
    validate_guides(report, chips_by_key)
    return report


def main() -> int:
    report = validate()
    for location, message in report.problems:
        print(f"{location}: {message}")
    if report:
        print(f"{len(report.problems)} problem(s)", file=sys.stderr)
        return 1
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
