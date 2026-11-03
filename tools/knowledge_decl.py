#!/usr/bin/env python3
"""Write wireskein's device declarations (decl/devices) from knowledge/:
what its identification reads, one TOML file per part, at
<bus>/<manufacturer>/<chip>.toml under OUT. Addresses come from
chips/<chip>.yaml; command or register names, answer lengths, the CRC and
the identifying register from knowledge/<chip>.yaml. Nothing else (timing,
behaviours, evidence) goes into the declaration.

    python3 tools/knowledge_decl.py OUT [CHIP ...]

With no CHIP, every knowledge file. wireskein copies OUT into its
src/wireskein/decl/devices and commits it.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent


def source_commit() -> str:
    def git(*args):
        return subprocess.run(["git", "-C", str(REPO_ROOT), *args], capture_output=True,
                              text=True, check=True).stdout.strip()
    commit = git("rev-parse", "--short", "HEAD")
    if git("status", "--porcelain", "--", "knowledge", "chips"):
        commit += "+uncommitted"
    return commit


def folder(manufacturer: str) -> str:
    """'QST Corporation' -> 'qst': the manufacturer's first word, lowercase."""
    return re.sub(r"[^a-z0-9-]", "", manufacturer.split()[0].lower())


def string(text: str) -> str:
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def render(chip: str, commit: str) -> tuple[Path, str]:
    data = yaml.safe_load((REPO_ROOT / "knowledge" / f"{chip}.yaml").read_text(encoding="utf-8"))
    catalog = yaml.safe_load((REPO_ROOT / "chips" / f"{chip}.yaml").read_text(encoding="utf-8"))
    bus = data["bus"]
    interface = next(i for i in catalog["interfaces"] if i["bus"] == bus)
    addresses = ", ".join("0x%02X" % int(a["addr"], 16) for a in interface["addresses"])
    addressing = data.get("addressing") or {}
    pointer_bytes = addressing["pointer_bits"] // 8
    commands = data.get("commands") or []
    lines = [f"# Generated from EmbedBenchDeviceData {commit} knowledge/{chip}.yaml and",
             f"# chips/{chip}.yaml by tools/knowledge_decl.py. Regenerate rather than edit.",
             "[device]",
             f"name = {string(catalog['name'])}",
             f"bus = {string(bus)}"]
    if commands:
        lines += ['kind = "command_table"', f"addresses = [{addresses}]",
                  f"command_bytes = {pointer_bytes}"]
        crc = addressing.get("crc")
        if crc:
            lines.append("crc = { poly = %s, init = %s, word = %d }" % (
                crc["poly"].upper().replace("0X", "0x"), crc["init"].upper().replace("0X", "0x"),
                crc["word"]))
        for command in commands:
            lines += ["", "[[command]]",
                      "code = 0x%0*X" % (pointer_bytes * 2, int(command["code"], 16)),
                      f"name = {string(command['name'])}"]
            if command["answer_bytes"]:
                lines.append(f"response = {command['answer_bytes']}")
    else:
        lines += ['kind = "register_map"', f"addresses = [{addresses}]",
                  f"pointer_bytes = {pointer_bytes}"]
        for reg in data["registers"]:
            if reg.get("identifies"):
                value = int(reg["reset"], 16)
                lines += ["", "[[identify]]", "register = 0x%02X" % int(reg["addr"], 16),
                          "value = 0x%0*X" % (reg["width"] * 2, value)]
        for reg in data["registers"]:
            lines += ["", "[[register]]", "address = 0x%02X" % int(reg["addr"], 16)]
            if reg.get("name"):
                lines.append(f"name = {string(reg['name'])}")
    path = Path(bus) / folder(catalog["manufacturer"]) / f"{chip}.toml"
    return path, "\n".join(lines) + "\n"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(sys.argv[1])
    chips = sys.argv[2:] or sorted(p.stem for p in (REPO_ROOT / "knowledge").glob("*.yaml"))
    commit = source_commit()
    for chip in chips:
        path, text = render(chip, commit)
        (out / path).parent.mkdir(parents=True, exist_ok=True)
        (out / path).write_text(text, encoding="utf-8")
        print(out / path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
