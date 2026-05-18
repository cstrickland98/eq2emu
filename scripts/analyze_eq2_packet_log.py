#!/usr/bin/env python3
"""Summarize JSONL output from frida_eq2_2006_packet_logger.js.

The logger gives raw events. This helper groups them into packet instances and
then into repeatable layouts by direction + type_id.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

try:
    from decode_eq2_packet_log import CLIENT_DERIVED_OPCODE_NAMES
except ImportError:
    CLIENT_DERIVED_OPCODE_NAMES: dict[int, str] = {}


UNKNOWN_TYPE = -1


@dataclass
class PacketInstance:
    seq: int
    direction: str = "unknown"
    type_id: int | None = None
    type_name: str | None = None
    length: int | None = None
    body_hex: str | None = None
    fields: list[dict[str, Any]] = field(default_factory=list)
    strings: list[dict[str, Any]] = field(default_factory=list)
    events: Counter[str] = field(default_factory=Counter)


def parse_json_line(line: str) -> dict[str, Any] | None:
    """Parse a JSON object even if Frida prompt text appears before it."""
    start = line.find("{")
    if start < 0:
        return None
    try:
        value = json.loads(line[start:])
    except json.JSONDecodeError:
        return None
    if isinstance(value, dict):
        return value
    return None


def iter_records(paths: Iterable[Path]) -> Iterable[dict[str, Any]]:
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                record = parse_json_line(line)
                if record is not None:
                    record["_source"] = str(path)
                    yield record


def load_registry(path: Path | None) -> dict[int, dict[str, str]]:
    if path is None or not path.exists():
        return {}

    registry: dict[int, dict[str, str]] = {}
    with path.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            try:
                type_id = int(row["type_id"])
            except (KeyError, ValueError):
                continue
            registry[type_id] = row
    return registry


def set_packet_type(packet: PacketInstance, record: dict[str, Any]) -> None:
    type_id = record.get("type_id")
    if isinstance(type_id, int):
        packet.type_id = type_id
    elif isinstance(type_id, str) and type_id.isdigit():
        packet.type_id = int(type_id)

    type_name = record.get("type_name")
    if isinstance(type_name, str) and type_name:
        packet.type_name = type_name


def get_packet(
    packets: dict[int, PacketInstance], record: dict[str, Any]
) -> PacketInstance | None:
    seq = record.get("seq")
    if not isinstance(seq, int):
        return None
    if seq not in packets:
        packets[seq] = PacketInstance(seq=seq)
    return packets[seq]


def build_instances(records: Iterable[dict[str, Any]]) -> dict[int, PacketInstance]:
    packets: dict[int, PacketInstance] = {}

    for record in records:
        packet = get_packet(packets, record)
        if packet is None:
            continue

        event = str(record.get("event", "unknown"))
        packet.events[event] += 1

        direction = record.get("direction")
        if isinstance(direction, str) and direction:
            packet.direction = direction

        set_packet_type(packet, record)

        if event == "packet_clear":
            length = record.get("length")
            if isinstance(length, int):
                packet.length = length
            body_hex = record.get("body_hex")
            if isinstance(body_hex, str) and body_hex:
                packet.body_hex = body_hex
        elif event in {"field_read", "field_write"}:
            packet.fields.append(record)
        elif event in {"string_read", "string_write"}:
            packet.strings.append(record)

    return packets


def short_hex(value: str | None, max_chars: int = 64) -> str:
    if not value:
        return ""
    if len(value) <= max_chars:
        return value
    return value[:max_chars] + "..."


def sample_value(record: dict[str, Any]) -> str:
    text = record.get("text")
    if isinstance(text, str):
        return repr(text[:80])

    value = record.get("value")
    if isinstance(value, dict):
        parts: list[str] = []
        for key in ("u8", "u16", "u32", "i32", "f32", "f64", "u64_hex"):
            if key in value:
                parts.append(f"{key}={value[key]}")
        if parts:
            return ", ".join(parts)
        bytes_hex = value.get("bytes_hex")
        if isinstance(bytes_hex, str):
            return f"bytes={short_hex(bytes_hex)}"

    bytes_hex = record.get("bytes_hex")
    if isinstance(bytes_hex, str):
        return f"bytes={short_hex(bytes_hex)}"

    return ""


def type_key(packet: PacketInstance) -> tuple[str, int]:
    return (packet.direction, packet.type_id if packet.type_id is not None else UNKNOWN_TYPE)


def format_length_summary(lengths: list[int]) -> str:
    if not lengths:
        return "unknown"
    counts = Counter(lengths)
    common = ", ".join(f"{length}x{count}" for length, count in counts.most_common(5))
    return f"min={min(lengths)} max={max(lengths)} unique={len(counts)} common={common}"


def registry_name(
    registry: dict[int, dict[str, str]], type_id: int | None, fallback: str | None
) -> str:
    if fallback:
        return fallback
    if type_id is None:
        return ""
    row = registry.get(type_id)
    if row is None:
        return CLIENT_DERIVED_OPCODE_NAMES.get(type_id, "")
    return (
        row.get("inferred_name", "")
        or row.get("metadata_name", "")
        or CLIENT_DERIVED_OPCODE_NAMES.get(type_id, "")
    )


def summarize_field_group(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, int | None, int | None], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        key = (
            str(record.get("event", "")),
            record.get("offset") if isinstance(record.get("offset"), int) else None,
            record.get("size") if isinstance(record.get("size"), int) else None,
        )
        grouped[key].append(record)

    rows: list[dict[str, Any]] = []
    for (event, offset, size), items in sorted(
        grouped.items(), key=lambda item: (item[0][1] is None, item[0][1] or -1, item[0][0], item[0][2] or -1)
    ):
        object_offsets = sorted(
            {
                item["object_offset"]
                for item in items
                if isinstance(item.get("object_offset"), int)
            }
        )
        samples: list[str] = []
        seen: set[str] = set()
        for item in items:
            sample = sample_value(item)
            if sample and sample not in seen:
                samples.append(sample)
                seen.add(sample)
            if len(samples) >= 4:
                break

        rows.append(
            {
                "event": event,
                "offset": offset,
                "size": size,
                "count": len(items),
                "object_offsets": object_offsets[:8],
                "samples": samples,
            }
        )
    return rows


def summarize(
    packets: dict[int, PacketInstance], registry: dict[int, dict[str, str]]
) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, int], list[PacketInstance]] = defaultdict(list)
    for packet in packets.values():
        grouped[type_key(packet)].append(packet)

    summaries: list[dict[str, Any]] = []
    for (direction, type_id_key), items in sorted(grouped.items()):
        type_id = None if type_id_key == UNKNOWN_TYPE else type_id_key
        type_names = Counter(item.type_name for item in items if item.type_name)
        type_name = registry_name(
            registry,
            type_id,
            type_names.most_common(1)[0][0] if type_names else None,
        )
        lengths = [item.length for item in items if item.length is not None]
        fields = [field for item in items for field in item.fields]
        strings = [string for item in items for string in item.strings]
        event_counts = Counter()
        for item in items:
            event_counts.update(item.events)

        registry_row = registry.get(type_id) if type_id is not None else None
        summaries.append(
            {
                "direction": direction,
                "type_id": type_id,
                "type_name": type_name,
                "packet_count": len(items),
                "lengths": {
                    "min": min(lengths) if lengths else None,
                    "max": max(lengths) if lengths else None,
                    "unique": sorted(set(lengths))[:32],
                    "summary": format_length_summary(lengths),
                },
                "event_counts": dict(event_counts),
                "registry": registry_row,
                "fields": summarize_field_group(fields),
                "strings": summarize_field_group(strings),
                "example_body_hex": short_hex(
                    next((item.body_hex for item in items if item.body_hex), None), 160
                ),
            }
        )
    return summaries


def write_json(path: Path, summaries: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(summaries, indent=2), encoding="utf-8")


def format_offset(offset: int | None) -> str:
    if offset is None:
        return "?"
    return f"0x{offset:04x}"


def markdown_table(rows: list[dict[str, Any]]) -> str:
    if not rows:
        return "_No field events captured._\n"

    lines = [
        "| Event | Offset | Size | Count | Object offsets | Samples |",
        "| --- | ---: | ---: | ---: | --- | --- |",
    ]
    for row in rows:
        samples = "<br>".join(str(sample).replace("|", "\\|") for sample in row["samples"])
        object_offsets = ", ".join(f"+0x{offset:x}" for offset in row["object_offsets"])
        lines.append(
            f"| `{row['event']}` | `{format_offset(row['offset'])}` | "
            f"`{'' if row['size'] is None else row['size']}` | {row['count']} | "
            f"`{object_offsets}` | {samples} |"
        )
    return "\n".join(lines) + "\n"


def write_markdown(path: Path, summaries: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["# EQ2 Packet Log Summary", ""]
    lines.append(
        "Generated from Frida JSONL logs. Treat inferred field types as candidates "
        "until confirmed against multiple captures and Ghidra deserializers."
    )
    lines.append("")

    for summary in summaries:
        type_id = summary["type_id"]
        title_id = "unknown" if type_id is None else str(type_id)
        type_name = summary["type_name"] or "unnamed"
        lines.append(f"## {summary['direction']} type {title_id} {type_name}")
        lines.append("")
        lines.append(f"- packets: {summary['packet_count']}")
        lines.append(f"- lengths: {summary['lengths']['summary']}")
        registry = summary.get("registry")
        if registry:
            lines.append(f"- serialize: `{registry.get('serialize', '')}`")
            lines.append(f"- deserialize: `{registry.get('deserialize', '')}`")
            lines.append(f"- vtable: `{registry.get('vtable', '')}`")
        if summary["example_body_hex"]:
            lines.append(f"- example body: `{summary['example_body_hex']}`")
        lines.append("")
        lines.append("### Fields")
        lines.append("")
        lines.append(markdown_table(summary["fields"]))
        if summary["strings"]:
            lines.append("### Strings")
            lines.append("")
            lines.append(markdown_table(summary["strings"]))
        lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="Frida JSONL log files")
    parser.add_argument(
        "--registry",
        type=Path,
        default=Path("artifacts/eq2_client_packet_registry.csv"),
        help="Optional packet registry CSV from extract_eq2_client_packet_registry.py",
    )
    parser.add_argument(
        "--out-md",
        type=Path,
        default=Path("artifacts/eq2_packet_log_summary.md"),
        help="Markdown report path",
    )
    parser.add_argument(
        "--out-json",
        type=Path,
        default=Path("artifacts/eq2_packet_log_summary.json"),
        help="JSON summary path",
    )
    args = parser.parse_args()

    registry = load_registry(args.registry)
    packets = build_instances(iter_records(args.logs))
    summaries = summarize(packets, registry)
    write_markdown(args.out_md, summaries)
    write_json(args.out_json, summaries)

    print(f"Read {len(packets)} packet instances from {len(args.logs)} log file(s).")
    print(f"Wrote Markdown: {args.out_md}")
    print(f"Wrote JSON: {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
