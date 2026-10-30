#!/usr/bin/env python3
"""Build the reverse indexes into build/ (not committed).

Which products carry a chip, which libraries drive it, which runs saw it and
whether it has a guide or a knowledge file are facts about the repository,
so they are derived here instead of being kept by hand in two places.

    python3 tools/build.py          # writes build/index.json and build/guides.ja.md
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate  # noqa: E402

REPO_ROOT = validate.REPO_ROOT
OUT = REPO_ROOT / "build"


def load(directory: str) -> dict[str, dict]:
    report = validate.Report()
    masters = validate.load_masters(directory, report)
    if report:
        raise SystemExit("run tools/validate.py first")
    return {data["key"]: data for data in masters.values()}


def first_line(path: Path) -> str:
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return path.stem


def index() -> dict:
    chips = load("chips")
    products = load("products")
    libraries = load("libraries")
    entry = {key: {"products": [], "libraries": [], "cases": [], "runs": [],
                   "guide": (REPO_ROOT / "guides" / "chips" / f"{key}.md").is_file(),
                   "knowledge": (REPO_ROOT / "knowledge" / f"{key}.yaml").is_file()}
             for key in chips}
    for key, product in products.items():
        for component in product.get("components") or []:
            entry[component["chip"]]["products"].append(key)
    for key, library in libraries.items():
        for chip in library["supports"]["chips"]:
            entry[chip]["libraries"].append(key)
    for case in sorted((REPO_ROOT / "cases").glob("*/case.yaml")):
        data = yaml.safe_load(case.read_text(encoding="utf-8"))
        for target in data.get("targets") or []:
            entry[target["chip"]]["cases"].append(data["key"])
    for provenance in sorted((REPO_ROOT / "runs").glob("*/provenance.yaml")):
        data = yaml.safe_load(provenance.read_text(encoding="utf-8"))
        for target in data.get("targets") or []:
            entry[target["chip"]]["runs"].append(provenance.parent.name)
    return {"chips": {key: {"name": chips[key]["name"], **value} for key, value in sorted(entry.items())}}


def guides_page(chips: dict) -> str:
    rows = [(key, value) for key, value in chips.items() if value["guide"]]
    missing = [key for key, value in chips.items() if not value["guide"]]
    lines = ["# デバイスガイド索引", "",
             "`tools/build.py` が生成する。書式と方針は guides/README.ja.md。", "",
             f"チップ {len(chips)} 件のうち **ガイドあり {len(rows)} 件 / 未執筆 {len(missing)} 件**。", "",
             "| チップ | 概要 | knowledge | run |", "|---|---|---|---|"]
    for key, value in rows:
        guide = REPO_ROOT / "guides" / "chips" / f"{key}.md"
        lines.append(f"| [{key}](../guides/chips/{key}.md) | {first_line(guide)} | "
                     f"{'✓' if value['knowledge'] else '—'} | {len(value['runs']) or '—'} |")
    if missing:
        lines += ["", "## ガイド未執筆", "", " ".join(f"`{key}`" for key in missing)]
    return "\n".join(lines) + "\n"


def main() -> int:
    data = index()
    OUT.mkdir(exist_ok=True)
    (OUT / "index.json").write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    (OUT / "guides.ja.md").write_text(guides_page(data["chips"]), encoding="utf-8")
    print(f"wrote {OUT.relative_to(REPO_ROOT)}/index.json and guides.ja.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
