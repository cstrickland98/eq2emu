#!/usr/bin/env python3
"""Extract opcode and struct metadata from the legacy EQ2Emu PacketParser."""

from __future__ import annotations

import argparse
import csv
import json
import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


OPCODE_INSERT_RE = re.compile(
    r"\((\d+),(\d+),(\d+),'((?:[^'\\]|\\.)*)',(\d+)\)"
)


@dataclass(frozen=True)
class OpcodeRow:
    row_id: int
    version_min: int
    version_max: int
    name: str
    opcode: int

    @property
    def is_base(self) -> bool:
        return self.version_min == 0 and self.version_max == 0

    def active_for(self, client_version: int) -> bool:
        return self.is_base or self.version_min <= client_version <= self.version_max


def parse_opcode_rows(sql_path: Path) -> list[OpcodeRow]:
    text = sql_path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"INSERT INTO `opcodes` VALUES (.*?);", text, re.S)
    if match is None:
        raise ValueError(f"Could not find opcodes INSERT in {sql_path}")

    rows: list[OpcodeRow] = []
    for item in OPCODE_INSERT_RE.finditer(match.group(1)):
        rows.append(
            OpcodeRow(
                row_id=int(item.group(1)),
                version_min=int(item.group(2)),
                version_max=int(item.group(3)),
                name=item.group(4).replace("\\'", "'"),
                opcode=int(item.group(5)),
            )
        )
    return rows


def select_active_opcodes(rows: Iterable[OpcodeRow], client_version: int) -> dict[str, OpcodeRow]:
    by_name: dict[str, list[OpcodeRow]] = {}
    for row in rows:
        if row.active_for(client_version):
            by_name.setdefault(row.name, []).append(row)

    selected: dict[str, OpcodeRow] = {}
    for name, candidates in by_name.items():
        specific = [row for row in candidates if not row.is_base]
        selected[name] = sorted(
            specific or candidates,
            key=lambda row: (row.version_min, row.version_max, row.row_id),
        )[-1]
    return selected


def data_node_to_dict(node: ET.Element) -> dict[str, Any]:
    value: dict[str, Any] = dict(node.attrib)
    children = [data_node_to_dict(child) for child in node.findall("Data")]
    if children:
        value["children"] = children
    return value


def parse_structs(xml_paths: Iterable[Path], client_version: int) -> list[dict[str, Any]]:
    structs: list[dict[str, Any]] = []
    for path in xml_paths:
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as exc:
            raise ValueError(f"Could not parse {path}: {exc}") from exc

        for node in root.findall("Struct"):
            version_text = node.attrib.get("ClientVersion", "0")
            try:
                version = int(version_text)
            except ValueError:
                continue
            if version > client_version:
                continue

            structs.append(
                {
                    "file": str(path),
                    "name": node.attrib.get("Name", ""),
                    "client_version": version,
                    "opcode_name": node.attrib.get("OpcodeName", ""),
                    "opcode_type": node.attrib.get("OpcodeType", ""),
                    "fields": [data_node_to_dict(child) for child in node.findall("Data")],
                }
            )
    return structs


def select_active_structs(structs: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    by_key: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    for struct in structs:
        key = (
            struct.get("name", ""),
            struct.get("opcode_name", ""),
            struct.get("opcode_type", ""),
        )
        by_key.setdefault(key, []).append(struct)

    selected: list[dict[str, Any]] = []
    for candidates in by_key.values():
        selected.append(
            sorted(candidates, key=lambda item: int(item.get("client_version", 0)))[-1]
        )
    return sorted(
        selected,
        key=lambda item: (
            item.get("opcode_name", ""),
            item.get("opcode_type", ""),
            item.get("name", ""),
        ),
    )


def write_opcode_csv(path: Path, opcodes: Iterable[OpcodeRow]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["opcode", "name", "version_min", "version_max", "row_id"],
        )
        writer.writeheader()
        for row in sorted(opcodes, key=lambda item: (item.opcode, item.name)):
            writer.writerow(
                {
                    "opcode": row.opcode,
                    "name": row.name,
                    "version_min": row.version_min,
                    "version_max": row.version_max,
                    "row_id": row.row_id,
                }
            )


def build_combined(
    active_opcodes: dict[str, OpcodeRow], active_structs: Iterable[dict[str, Any]]
) -> list[dict[str, Any]]:
    combined: list[dict[str, Any]] = []
    for struct in active_structs:
        opcode_name = struct.get("opcode_name", "")
        opcode = active_opcodes.get(opcode_name) if opcode_name else None
        item = dict(struct)
        item["opcode"] = opcode.opcode if opcode is not None else None
        item["opcode_version_min"] = opcode.version_min if opcode is not None else None
        item["opcode_version_max"] = opcode.version_max if opcode is not None else None
        combined.append(item)
    return combined


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--packetparser-dir",
        type=Path,
        default=Path(r"E:\_EQ2\eq2emu-tools\PacketParser"),
        help="Legacy PacketParser directory",
    )
    parser.add_argument(
        "--client-version",
        type=int,
        default=546,
        help="EQ2 client version/build used for selecting ranged opcode/struct definitions",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path("artifacts/packetparser_metadata"),
        help="Output directory",
    )
    args = parser.parse_args()

    sql_path = args.packetparser_dir / "Parser DB.sql"
    xml_paths = sorted((args.packetparser_dir / "ParserStructs").glob("*.xml"))

    rows = parse_opcode_rows(sql_path)
    active_opcodes = select_active_opcodes(rows, args.client_version)
    active_structs = select_active_structs(parse_structs(xml_paths, args.client_version))
    combined = build_combined(active_opcodes, active_structs)

    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    write_opcode_csv(out_dir / f"opcodes_v{args.client_version}.csv", active_opcodes.values())
    (out_dir / f"opcodes_v{args.client_version}.json").write_text(
        json.dumps(
            [
                {
                    "opcode": row.opcode,
                    "name": row.name,
                    "version_min": row.version_min,
                    "version_max": row.version_max,
                    "row_id": row.row_id,
                }
                for row in sorted(active_opcodes.values(), key=lambda item: (item.opcode, item.name))
            ],
            indent=2,
        ),
        encoding="utf-8",
    )
    (out_dir / f"structs_v{args.client_version}.json").write_text(
        json.dumps(active_structs, indent=2), encoding="utf-8"
    )
    (out_dir / f"structs_by_opcode_v{args.client_version}.json").write_text(
        json.dumps(combined, indent=2), encoding="utf-8"
    )

    print(f"Parsed opcode rows: {len(rows)}")
    print(f"Active opcode names for client {args.client_version}: {len(active_opcodes)}")
    print(f"Active structs for client {args.client_version}: {len(active_structs)}")
    print(f"Wrote: {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
