#!/usr/bin/env python3
"""Read-only inventory of 64-bit little-endian Mach-O and universal containers.

This is a structural inventory, not a complete Mach-O validator or disassembler.
Unknown load commands are skipped after validating their declared bounds. Symbol
names are omitted unless --include-named-functions is explicitly supplied.
Output contains addresses and belongs in local, ignored analysis artifacts.

Format references (field layout/constants only; implementation is original):
https://github.com/apple-oss-distributions/cctools/blob/main/include/mach-o/loader.h
https://github.com/apple-oss-distributions/cctools/blob/main/include/mach-o/fat.h
https://github.com/apple-oss-distributions/cctools/blob/main/include/mach-o/nlist.h
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import uuid


class MachOError(ValueError):
    """Malformed or unsupported input."""


CPU_NAMES = {0x01000007: "x86_64", 0x0100000C: "arm64"}
FAT_MAGICS = {
    b"\xca\xfe\xba\xbe": (">", False),
    b"\xbe\xba\xfe\xca": ("<", False),
    b"\xca\xfe\xba\xbf": (">", True),
    b"\xbf\xba\xfe\xca": ("<", True),
}
MAX_U64 = (1 << 64) - 1
ZERO_FILL_TYPES = {0x1, 0xC, 0x12}


def checked_range(total, offset, size, label):
    if offset < 0 or size < 0 or offset > total or size > total - offset:
        raise MachOError(f"{label} lies outside its containing byte range")


def checked_address(address, size, label):
    if address > MAX_U64 or size > (1 << 64) - address:
        raise MachOError(f"{label} overflows the 64-bit address space")


def fixed_name(raw):
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def parse_function_starts(data, image_base, executable_ranges):
    """Decode a NUL-terminated series of unsigned 64-bit ULEB deltas."""
    addresses = []
    if not data:
        return addresses
    if image_base is None:
        raise MachOError("function starts require a unique __TEXT segment")
    cursor = 0
    address = image_base
    while cursor < len(data):
        delta = 0
        for index in range(10):
            if cursor == len(data):
                raise MachOError("truncated function-start ULEB value")
            byte = data[cursor]
            cursor += 1
            if index == 9 and byte > 1:
                raise MachOError("function-start ULEB value exceeds 64 bits")
            delta |= (byte & 0x7F) << (7 * index)
            if not byte & 0x80:
                break
        else:
            raise MachOError("unterminated function-start ULEB value")
        if delta == 0:
            if any(data[cursor:]):
                raise MachOError("nonzero data after function-start terminator")
            return addresses
        if delta > MAX_U64 - address:
            raise MachOError("function-start address overflows 64 bits")
        address += delta
        if not any(start <= address < end for start, end in executable_ranges):
            raise MachOError("function start is outside executable segments")
        addresses.append(f"0x{address:x}")
    raise MachOError("missing function-start terminator")


def inspect_slice(data, file_offset, include_named_functions):
    checked_range(len(data), 0, 32, "Mach-O header")
    header = struct.unpack_from("<8I", data)
    magic, cpu, subtype, file_type, ncmds, sizeofcmds, flags, _ = header
    if magic != 0xFEEDFACF:
        raise MachOError("only little-endian 64-bit Mach-O slices are supported")
    if cpu not in CPU_NAMES:
        raise MachOError(f"unsupported CPU type 0x{cpu:x}")
    checked_range(len(data), 32, sizeofcmds, "load-command table")
    if ncmds > sizeofcmds // 8:
        raise MachOError("load-command count exceeds table capacity")
    result = {
        "architecture": CPU_NAMES[cpu], "cpu_type": cpu, "cpu_subtype": subtype,
        "offset": file_offset, "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "file_type": file_type, "flags": flags, "uuid": None, "segments": [],
    }
    sections = []
    symbol_command = None
    starts_command = None
    cursor = 32
    command_end = cursor + sizeofcmds
    for _ in range(ncmds):
        checked_range(command_end, cursor, 8, "load-command header")
        cmd, cmdsize = struct.unpack_from("<II", data, cursor)
        if cmdsize < 8 or cmdsize % 8:
            raise MachOError("invalid 64-bit load-command size/alignment")
        checked_range(command_end, cursor, cmdsize, "load command")
        if cmd == 0x1B:  # LC_UUID
            if cmdsize != 24 or result["uuid"] is not None:
                raise MachOError("invalid or duplicate LC_UUID")
            result["uuid"] = str(uuid.UUID(bytes=bytes(data[cursor + 8:cursor + 24])))
        elif cmd == 0x19:  # LC_SEGMENT_64
            if cmdsize < 72:
                raise MachOError("truncated LC_SEGMENT_64")
            fields = struct.unpack_from("<16sQQQQiiII", data, cursor + 8)
            raw_name, vmaddr, vmsize, offset, size, maxprot, initprot, count, segflags = fields
            if count > (cmdsize - 72) // 80 or 72 + count * 80 != cmdsize:
                raise MachOError("section count does not match segment command")
            checked_range(len(data), offset, size, "segment contents")
            checked_address(vmaddr, vmsize, "segment")
            segment = {
                "name": fixed_name(raw_name), "address": f"0x{vmaddr:x}",
                "vm_size": vmsize, "file_offset": offset,
                "absolute_file_offset": file_offset + offset, "file_size": size,
                "max_protection": maxprot, "initial_protection": initprot,
                "flags": segflags, "sections": [],
            }
            for index in range(count):
                fields = struct.unpack_from("<16s16sQQ8I", data, cursor + 72 + index * 80)
                raw_section, raw_segment, addr, length, off, align, reloff, nreloc, secflags, _, _, _ = fields
                checked_address(addr, length, "section")
                if addr < vmaddr or length > vmaddr + vmsize - addr:
                    raise MachOError("section address range lies outside segment")
                if align > 63:
                    raise MachOError("section alignment exceeds 64-bit range")
                zerofill = (secflags & 0xFF) in ZERO_FILL_TYPES
                if not zerofill:
                    checked_range(len(data), off, length, "section contents")
                    if length and (off < offset or length > offset + size - off):
                        raise MachOError("section file range lies outside segment")
                checked_range(len(data), reloff, nreloc * 8, "section relocations")
                section = {
                    "index": len(sections) + 1,
                    "name": fixed_name(raw_section), "segment": fixed_name(raw_segment),
                    "address": f"0x{addr:x}", "size": length,
                    "file_offset": None if zerofill else off,
                    "absolute_file_offset": None if zerofill else file_offset + off,
                    "alignment_power": align, "flags": secflags,
                    "zero_fill": zerofill, "relocation_count": nreloc,
                }
                sections.append(section)
                segment["sections"].append(section)
            result["segments"].append(segment)
        elif cmd == 0x2:  # LC_SYMTAB
            if cmdsize != 24 or symbol_command is not None:
                raise MachOError("invalid or duplicate LC_SYMTAB")
            symbol_command = struct.unpack_from("<4I", data, cursor + 8)
        elif cmd == 0x26:  # LC_FUNCTION_STARTS
            if cmdsize != 16 or starts_command is not None:
                raise MachOError("invalid or duplicate LC_FUNCTION_STARTS")
            starts_command = struct.unpack_from("<II", data, cursor + 8)
        cursor += cmdsize
    if cursor != command_end:
        raise MachOError("load-command count does not consume declared table")

    result["no_dwarf"] = not any(
        segment["name"] == "__DWARF" or any(
            section["name"].startswith(("__debug_", "__zdebug_"))
            or section["flags"] & 0x02000000 for section in segment["sections"]
        ) for segment in result["segments"]
    )
    result["no_dwarf_scope"] = "embedded section inventory only; external dSYM not checked"
    symbols = {"present": symbol_command is not None, "total": 0,
               "defined": 0, "undefined": 0, "stab": 0, "other": 0}
    named_functions = []
    if symbol_command is not None:
        symoff, count, stroff, strsize = symbol_command
        checked_range(len(data), symoff, count * 16, "symbol table")
        checked_range(len(data), stroff, strsize, "string table")
        strings = bytes(data[stroff:stroff + strsize])
        if strings and strings[-1] != 0:
            raise MachOError("string table is not NUL terminated")
        symbols["total"] = count
        for index in range(count):
            strx, kind, section_index, _, value = struct.unpack_from("<IBBHQ", data, symoff + index * 16)
            if strx and strx >= strsize:
                raise MachOError("symbol string index lies outside string table")
            if kind & 0xE0:  # N_STAB takes precedence over type bits.
                symbols["stab"] += 1
                continue
            ntype = kind & 0x0E
            if ntype in (0x0, 0xC):  # N_UNDF and N_PBUD
                symbols["undefined"] += 1
            elif ntype in (0x2, 0xE, 0xA):  # N_ABS, N_SECT, N_INDR
                symbols["defined"] += 1
            else:
                symbols["other"] += 1
            if ntype == 0xE:
                if not 1 <= section_index <= len(sections):
                    raise MachOError("defined symbol has invalid section index")
                section = sections[section_index - 1]
                text = section["name"] == "__text" or section["flags"] & 0x80000400
                if include_named_functions and text and strx:
                    start = int(section["address"], 16)
                    if start <= value < start + section["size"]:
                        end = strings.find(b"\0", strx)
                        # Bound per-entry JSON expansion for hostile name tables.
                        if end - strx > 1024 * 1024:
                            raise MachOError("symbol name exceeds 1 MiB export limit")
                        named_functions.append({
                            "name": strings[strx:end].decode("utf-8", errors="replace"),
                            "address": f"0x{value:x}", "section_index": section_index,
                        })
    result["symbol_table"] = symbols
    if include_named_functions:
        result["named_text_symbols"] = named_functions
        result["named_text_symbols_note"] = "section symbols are candidates, not proven function boundaries"
    text_segments = [segment for segment in result["segments"] if segment["name"] == "__TEXT"]
    image_base = int(text_segments[0]["address"], 16) if len(text_segments) == 1 else None
    starts = []
    if starts_command is not None:
        offset, size = starts_command
        checked_range(len(data), offset, size, "function-start data")
        executable = [(int(s["address"], 16), int(s["address"], 16) + s["vm_size"])
                      for s in result["segments"] if s["initial_protection"] & 4]
        starts = parse_function_starts(data[offset:offset + size], image_base, executable)
    result["function_starts"] = {
        "present": starts_command is not None, "count": len(starts),
        "image_base": None if image_base is None else f"0x{image_base:x}",
        "addresses": starts,
    }
    return result


def inspect_bytes(data, include_named_functions=False):
    """Inventory a bytes-like object; never modifies or executes the input."""
    view = memoryview(data)
    checked_range(len(view), 0, 4, "magic")
    magic = bytes(view[:4])
    slices = []
    if magic in FAT_MAGICS:
        endian, fat64 = FAT_MAGICS[magic]
        checked_range(len(view), 0, 8, "universal header")
        count = struct.unpack_from(endian + "I", view, 4)[0]
        entry_size = 32 if fat64 else 20
        checked_range(len(view), 8, count * entry_size, "universal architecture table")
        if count == 0:
            raise MachOError("universal file has no slices")
        table_end = 8 + count * entry_size
        entries = []
        for index in range(count):
            values = struct.unpack_from(endian + ("IIQQII" if fat64 else "IIIII"), view, 8 + index * entry_size)
            cpu, subtype, offset, size, align = values[:5]
            checked_range(len(view), offset, size, "universal slice")
            if offset < table_end or size < 32:
                raise MachOError("universal slice overlaps table or lacks header")
            if align > 63 or offset % (1 << align):
                raise MachOError("invalid universal slice alignment")
            entries.append((offset, size, cpu, subtype))
        ordered = sorted(entries)
        for previous, current in zip(ordered, ordered[1:]):
            if previous[0] + previous[1] > current[0]:
                raise MachOError("universal slices overlap")
        for offset, size, cpu, subtype in entries:
            item = inspect_slice(view[offset:offset + size], offset, include_named_functions)
            if item["cpu_type"] != cpu or item["cpu_subtype"] != subtype:
                raise MachOError("universal CPU metadata disagrees with slice")
            slices.append(item)
        container_format = "fat64" if fat64 else "fat32"
    else:
        slices.append(inspect_slice(view, 0, include_named_functions))
        container_format = "mach-o64"
    return {"schema_version": 1, "format": container_format, "file_size": len(view),
            "sha256": hashlib.sha256(view).hexdigest(), "slices": slices}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--output", type=Path, help="create a new JSON file; existing files are never overwritten")
    parser.add_argument("--include-named-functions", action="store_true",
                        help="include named text-section symbol candidates in local analysis output")
    args = parser.parse_args(argv)
    try:
        report = inspect_bytes(args.binary.read_bytes(), args.include_named_functions)
        rendered = json.dumps(report, indent=2) + "\n"
        if args.output:
            with args.output.open("x", encoding="utf-8") as destination:
                destination.write(rendered)
        else:
            sys.stdout.write(rendered)
        return 0
    except (OSError, MachOError) as error:
        print(f"inspect_macho: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
