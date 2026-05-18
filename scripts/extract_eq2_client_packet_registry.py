#!/usr/bin/env python3
"""Extract EQ2 2006 client packet/message registry metadata from EverQuest2.exe.

This is a static helper for the Ghidra findings in
docs/eq2_2006_client_packet_reversing_notes.md. It scans the PE image for the
VeType static initializer pattern, follows the known EqNetMsgs registration
functions, and emits a type-id table with factory, vtable, serialize, and
deserialize addresses.

It intentionally does not require Ghidra or third-party packages.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


IMAGE_BASE = 0x00400000

VETYPE_CTOR = 0x00985A66
VETYPE_REGISTER = 0x00985F01
MALLOC_LIKE = 0x00999C08

COMMON_REGISTER_INIT = 0x00973806
EQNETMSGS_REGISTER_INIT = 0x00917970


@dataclass(frozen=True)
class Section:
    name: str
    va: int
    size: int
    raw_offset: int
    raw_size: int


_X86_PREFIXES = {
    0x26,
    0x2E,
    0x36,
    0x3E,
    0x64,
    0x65,
    0x66,
    0x67,
    0xF0,
    0xF2,
    0xF3,
}


def _modrm_length(data: bytes, pos: int, end: int, address16: bool = False) -> int:
    if pos >= end:
        return 0

    modrm = data[pos]
    mod = modrm >> 6
    rm = modrm & 0x07
    length = 1

    if address16:
        if mod == 0 and rm == 6:
            length += 2
        elif mod == 1:
            length += 1
        elif mod == 2:
            length += 2
        return min(length, end - pos)

    if mod != 3 and rm == 4:
        if pos + length >= end:
            return min(length, end - pos)
        sib = data[pos + length]
        base = sib & 0x07
        length += 1
        if mod == 0 and base == 5:
            length += 4
    elif mod == 0 and rm == 5:
        length += 4
    elif mod == 1:
        length += 1
    elif mod == 2:
        length += 4

    return min(length, end - pos)


def _x86_instruction_length(data: bytes, pos: int, end: int) -> int:
    start = pos
    operand16 = False
    address16 = False
    while pos < end and data[pos] in _X86_PREFIXES:
        operand16 = operand16 or data[pos] == 0x66
        address16 = address16 or data[pos] == 0x67
        pos += 1
    if pos >= end:
        return end - start

    opcode = data[pos]
    pos += 1
    imm_word = 2 if operand16 else 4

    if opcode == 0x0F:
        if pos >= end:
            return end - start
        opcode2 = data[pos]
        pos += 1
        if 0x80 <= opcode2 <= 0x8F:
            pos += imm_word
        elif opcode2 in {
            0x00,
            0x01,
            0x02,
            0x03,
            0x10,
            0x11,
            0x12,
            0x13,
            0x18,
            0x19,
            0x1A,
            0x1B,
            0x1C,
            0x1D,
            0x1E,
            0x1F,
            0x20,
            0x21,
            0x22,
            0x23,
            0x40,
            0x41,
            0x42,
            0x43,
            0x44,
            0x45,
            0x46,
            0x47,
            0x48,
            0x49,
            0x4A,
            0x4B,
            0x4C,
            0x4D,
            0x4E,
            0x4F,
            0x90,
            0x91,
            0x92,
            0x93,
            0x94,
            0x95,
            0x96,
            0x97,
            0x98,
            0x99,
            0x9A,
            0x9B,
            0x9C,
            0x9D,
            0x9E,
            0x9F,
            0xA3,
            0xA4,
            0xA5,
            0xAB,
            0xAC,
            0xAD,
            0xAF,
            0xB0,
            0xB1,
            0xB2,
            0xB3,
            0xB6,
            0xB7,
            0xBA,
            0xBB,
            0xBC,
            0xBD,
            0xBE,
            0xBF,
            0xC1,
        }:
            modrm_pos = pos
            modrm_len = _modrm_length(data, pos, end, address16)
            pos += modrm_len
            if opcode2 in {0xA4, 0xAC, 0xBA} and modrm_len:
                pos += 1
        return min(pos - start, end - start)

    if opcode in {0xC3, 0xC9, 0xCB, 0xCC, 0xF4, 0xF5, 0xF8, 0xF9, 0xFC, 0xFD}:
        return pos - start
    if opcode in {0xC2, 0xCA}:
        pos += 2
        return min(pos - start, end - start)
    if 0x50 <= opcode <= 0x5F or 0x40 <= opcode <= 0x4F or 0x90 <= opcode <= 0x9F:
        return pos - start
    if 0xB0 <= opcode <= 0xB7:
        pos += 1
        return min(pos - start, end - start)
    if 0xB8 <= opcode <= 0xBF:
        pos += imm_word
        return min(pos - start, end - start)
    if opcode in {0x68, 0xA1, 0xA3, 0xE8, 0xE9}:
        pos += imm_word
        return min(pos - start, end - start)
    if opcode in {0x6A, 0xA0, 0xA2, 0xCD, 0xD4, 0xD5, 0xE3, 0xEB} or 0x70 <= opcode <= 0x7F:
        pos += 1
        return min(pos - start, end - start)
    if 0xE0 <= opcode <= 0xE2:
        pos += 1
        return min(pos - start, end - start)
    if opcode in {0x9A, 0xEA}:
        pos += 4 if operand16 else 6
        return min(pos - start, end - start)
    if opcode in {0xC8}:
        pos += 3
        return min(pos - start, end - start)
    if opcode in {0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C}:
        pos += 1
        return min(pos - start, end - start)
    if opcode in {0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D}:
        pos += imm_word
        return min(pos - start, end - start)

    modrm_immediate: dict[int, int] = {
        0x69: imm_word,
        0x6B: 1,
        0x80: 1,
        0x81: imm_word,
        0x82: 1,
        0x83: 1,
        0xC0: 1,
        0xC1: 1,
        0xC6: 1,
        0xC7: imm_word,
    }
    if opcode in modrm_immediate:
        pos += _modrm_length(data, pos, end, address16)
        pos += modrm_immediate[opcode]
        return min(pos - start, end - start)

    modrm_only = {
        0x00,
        0x01,
        0x02,
        0x03,
        0x08,
        0x09,
        0x0A,
        0x0B,
        0x10,
        0x11,
        0x12,
        0x13,
        0x18,
        0x19,
        0x1A,
        0x1B,
        0x20,
        0x21,
        0x22,
        0x23,
        0x28,
        0x29,
        0x2A,
        0x2B,
        0x30,
        0x31,
        0x32,
        0x33,
        0x38,
        0x39,
        0x3A,
        0x3B,
        0x62,
        0x63,
        0x84,
        0x85,
        0x86,
        0x87,
        0x88,
        0x89,
        0x8A,
        0x8B,
        0x8C,
        0x8D,
        0x8E,
        0x8F,
        0xD0,
        0xD1,
        0xD2,
        0xD3,
        0xD8,
        0xD9,
        0xDA,
        0xDB,
        0xDC,
        0xDD,
        0xDE,
        0xDF,
        0xFE,
        0xFF,
    }
    if opcode in modrm_only:
        pos += _modrm_length(data, pos, end, address16)
        return min(pos - start, end - start)

    if opcode in {0xF6, 0xF7}:
        modrm_pos = pos
        modrm_len = _modrm_length(data, pos, end, address16)
        pos += modrm_len
        if modrm_len and (data[modrm_pos] >> 3) & 0x07 in {0, 1}:
            pos += 1 if opcode == 0xF6 else imm_word
        return min(pos - start, end - start)

    return max(1, pos - start)


def _x86_is_return(data: bytes, pos: int, end: int) -> bool:
    while pos < end and data[pos] in _X86_PREFIXES:
        pos += 1
    return pos < end and data[pos] in {0xC2, 0xC3, 0xCA, 0xCB}


class PeImage:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        self.sections = self._read_sections()

    def _read_sections(self) -> list[Section]:
        data = self.data
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
        optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
        section_offset = pe_offset + 24 + optional_size

        sections: list[Section] = []
        for index in range(section_count):
            offset = section_offset + index * 40
            name = data[offset : offset + 8].split(b"\0")[0].decode("ascii", "ignore")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
                "<IIII", data, offset + 8
            )
            sections.append(
                Section(
                    name=name,
                    va=IMAGE_BASE + virtual_address,
                    size=max(virtual_size, raw_size),
                    raw_offset=raw_offset,
                    raw_size=raw_size,
                )
            )
        return sections

    def va_to_offset(self, va: int) -> int | None:
        for section in self.sections:
            if section.va <= va < section.va + section.size:
                delta = va - section.va
                if delta >= section.raw_size:
                    return None
                return section.raw_offset + delta
        return None

    def offset_to_va(self, offset: int) -> int | None:
        for section in self.sections:
            if section.raw_offset <= offset < section.raw_offset + section.raw_size:
                return section.va + (offset - section.raw_offset)
        return None

    def section_name(self, va: int) -> str:
        for section in self.sections:
            if section.va <= va < section.va + section.size:
                return section.name
        return ""

    def is_text(self, va: int) -> bool:
        return self.section_name(va) == ".text"

    def is_rdata(self, va: int) -> bool:
        return self.section_name(va) == ".rdata"

    def u32(self, va: int) -> int | None:
        offset = self.va_to_offset(va)
        if offset is None or offset + 4 > len(self.data):
            return None
        return struct.unpack_from("<I", self.data, offset)[0]

    def c_string(self, va: int, max_len: int = 512) -> str | None:
        offset = self.va_to_offset(va)
        if offset is None:
            return None
        end_limit = min(len(self.data), offset + max_len)
        end = self.data.find(b"\0", offset, end_limit)
        if end < 0:
            end = end_limit
        raw = self.data[offset:end]
        if any(byte < 9 or 13 < byte < 32 for byte in raw):
            return None
        try:
            return raw.decode("ascii")
        except UnicodeDecodeError:
            return None

    def function_bytes(self, va: int, max_len: int = 0x700) -> bytes:
        offset = self.va_to_offset(va)
        if offset is None:
            return b""
        end = min(len(self.data), offset + max_len)
        pos = offset
        while pos < end:
            instruction_len = _x86_instruction_length(self.data, pos, end)
            if _x86_is_return(self.data, pos, end):
                return self.data[offset : min(end, pos + instruction_len)]
            pos += max(1, instruction_len)
        return self.data[offset:end]


def signed_push_imm8(byte_value: int) -> int:
    return struct.unpack("b", bytes([byte_value]))[0]


def call_target_at(image: PeImage, file_offset: int) -> int | None:
    call_va = image.offset_to_va(file_offset)
    if call_va is None or file_offset + 5 > len(image.data):
        return None
    rel = struct.unpack_from("<i", image.data, file_offset + 1)[0]
    return call_va + 5 + rel


def find_type_initializers(image: PeImage) -> dict[int, dict[str, object]]:
    """Return metadata VA -> static VeType initializer record."""
    records: dict[int, dict[str, object]] = {}
    data = image.data

    for start in range(len(data) - 32):
        pos = start
        if data[pos] == 0x6A:
            type_id = signed_push_imm8(data[pos + 1])
            pos += 2
        elif data[pos] == 0x68:
            type_id = struct.unpack_from("<I", data, pos + 1)[0]
            pos += 5
        else:
            continue

        if (
            pos + 20 > len(data)
            or data[pos] != 0x68
            or data[pos + 5] != 0x68
            or data[pos + 10] != 0xB9
            or data[pos + 15] != 0xE8
        ):
            continue

        factory = struct.unpack_from("<I", data, pos + 1)[0]
        name_ptr = struct.unpack_from("<I", data, pos + 6)[0]
        meta = struct.unpack_from("<I", data, pos + 11)[0]
        target = call_target_at(image, pos + 15)
        if target != VETYPE_CTOR:
            continue

        records[meta] = {
            "type_id": type_id,
            "meta": meta,
            "factory": factory,
            "name_ptr": name_ptr,
            "metadata_name": image.c_string(name_ptr) or "",
        }

    return records


def registered_metas(image: PeImage, function_va: int, max_len: int) -> list[int]:
    """Extract metadata VAs passed to VeType_Register_Maybe.

    The registration functions use one initial PUSH followed by repeated
    MOV [ESP], imm32 / CALL patterns.
    """
    offset = image.va_to_offset(function_va)
    if offset is None:
        return []

    out: list[int] = []
    pos = offset
    end = min(len(image.data) - 10, offset + max_len)
    while pos < end:
        meta = None
        call_offset = None
        if image.data[pos] == 0x68 and image.data[pos + 5] == 0xE8:
            meta = struct.unpack_from("<I", image.data, pos + 1)[0]
            call_offset = pos + 5
        elif image.data[pos : pos + 3] == b"\xC7\x04\x24" and image.data[pos + 7] == 0xE8:
            meta = struct.unpack_from("<I", image.data, pos + 3)[0]
            call_offset = pos + 7

        if call_offset is not None and call_target_at(image, call_offset) == VETYPE_REGISTER:
            out.append(int(meta))
            pos = call_offset + 5
            continue

        if image.data[pos] == 0xC3 and out:
            break
        pos += 1

    return out


def find_vtable_from_factory(image: PeImage, factory_va: int, depth: int = 0, seen: set[int] | None = None) -> int | None:
    if seen is None:
        seen = set()
    if depth > 3 or factory_va in seen:
        return None
    seen.add(factory_va)

    offset = image.va_to_offset(factory_va)
    if offset is None:
        return None

    blob = image.function_bytes(factory_va, 220)
    candidates: list[tuple[int, int, int]] = []
    for index in range(len(blob) - 6):
        # MOV dword ptr [EAX/ECX/ESI], imm32 is common for vtable assignment.
        if blob[index : index + 2] not in (b"\xC7\x00", b"\xC7\x01", b"\xC7\x06"):
            continue
        imm = struct.unpack_from("<I", blob, index + 2)[0]
        if not image.is_rdata(imm):
            continue
        ptrs = [image.u32(imm + item * 4) for item in range(5)]
        score = sum(1 for ptr in ptrs if ptr is not None and image.is_text(ptr))
        if score >= 3:
            candidates.append((index, imm, score))

    first_candidate_index = min((item[0] for item in candidates), default=None)
    has_malloc_call = False
    for index in range(len(blob) - 5):
        if blob[index] != 0xE8:
            continue
        target = factory_va + index + 5 + struct.unpack_from("<i", blob, index + 1)[0]
        if target == MALLOC_LIKE and (
            first_candidate_index is None or index < first_candidate_index
        ):
            has_malloc_call = True
            break

    if candidates and not has_malloc_call:
        return sorted(candidates, key=lambda item: (-item[2], item[0]))[0][1]

    # If the factory just allocates then calls a constructor, follow calls/jumps.
    for index in range(len(blob) - 5):
        if blob[index] not in (0xE8, 0xE9):
            continue
        target = factory_va + index + 5 + struct.unpack_from("<i", blob, index + 1)[0]
        if image.is_text(target) and target != MALLOC_LIKE:
            vtable = find_vtable_from_factory(image, target, depth + 1, seen)
            if vtable is not None:
                return vtable

    if candidates:
        return sorted(candidates, key=lambda item: (-item[2], item[0]))[0][1]

    return None


def interesting_strings_in_function(image: PeImage, function_va: int) -> list[str]:
    blob = image.function_bytes(function_va)
    strings: list[str] = []
    seen: set[int] = set()
    for index in range(len(blob) - 4):
        ptr = struct.unpack_from("<I", blob, index)[0]
        if ptr in seen or not image.is_rdata(ptr):
            continue
        seen.add(ptr)
        value = image.c_string(ptr, 320)
        if not value:
            continue
        if any(
            marker in value
            for marker in (
                "Msg::",
                "deserialize",
                "serialize",
                "too big",
                "Unsupported",
                "Invalid dispatch",
            )
        ):
            strings.append(value)
    return strings


def infer_message_name(strings: Iterable[str]) -> str:
    for value in strings:
        match = re.search(r"([A-Za-z_][A-Za-z0-9_]*Msg)::(?:de)?serialize", value)
        if match:
            return match.group(1)
    return ""


def build_rows(image: PeImage) -> list[dict[str, object]]:
    records = find_type_initializers(image)
    metas = registered_metas(image, COMMON_REGISTER_INIT, 0x200)
    metas.extend(registered_metas(image, EQNETMSGS_REGISTER_INIT, 0x2000))

    rows: list[dict[str, object]] = []
    for meta in metas:
        record = records[meta]
        vtable = find_vtable_from_factory(image, int(record["factory"]))
        serialize = image.u32(vtable + 4) if vtable is not None else None
        deserialize = image.u32(vtable + 8) if vtable is not None else None
        type_info = image.u32(vtable + 0x0C) if vtable is not None else None

        strings: list[str] = []
        if deserialize is not None:
            strings.extend(interesting_strings_in_function(image, deserialize))
        if serialize is not None:
            strings.extend(interesting_strings_in_function(image, serialize))

        rows.append(
            {
                **record,
                "vtable": vtable,
                "serialize": serialize,
                "deserialize": deserialize,
                "type_info_function": type_info,
                "inferred_name": infer_message_name(strings),
                "evidence": strings[:4],
            }
        )

    return rows


def hex_or_empty(value: object) -> str:
    if isinstance(value, int):
        return f"0x{value:08x}"
    return ""


def write_csv(rows: list[dict[str, object]], path: Path) -> None:
    fieldnames = [
        "type_id",
        "inferred_name",
        "meta",
        "factory",
        "vtable",
        "serialize",
        "deserialize",
        "type_info_function",
        "metadata_name",
        "evidence",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    key: (
                        "; ".join(row[key])
                        if key == "evidence"
                        else hex_or_empty(row[key])
                        if key not in ("type_id", "inferred_name", "metadata_name")
                        else row[key]
                    )
                    for key in fieldnames
                }
            )


def write_json(rows: list[dict[str, object]], path: Path) -> None:
    path.write_text(json.dumps(rows, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path, help="Path to EverQuest2.exe")
    parser.add_argument("--csv", type=Path, help="Optional CSV output path")
    parser.add_argument("--json", type=Path, help="Optional JSON output path")
    args = parser.parse_args()

    image = PeImage(args.exe)
    rows = build_rows(image)
    rows.sort(key=lambda row: int(row["type_id"]))

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        write_csv(rows, args.csv)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        write_json(rows, args.json)

    inferred = sum(1 for row in rows if row["inferred_name"])
    print(f"Extracted {len(rows)} registered packet/message types.")
    print(f"Type ID range: {rows[0]['type_id']}..{rows[-1]['type_id']}")
    print(f"Rows with names inferred from method strings: {inferred}")
    if args.csv:
        print(f"Wrote CSV: {args.csv}")
    if args.json:
        print(f"Wrote JSON: {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
