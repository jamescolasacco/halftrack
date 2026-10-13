#!/usr/bin/env python3
"""Synthetic-format regressions; no application binaries or private fixtures."""

import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "inspect_macho", Path(__file__).resolve().parents[1] / "scripts" / "inspect_macho.py")
macho = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(macho)

BASE = 0x100000000
ARM64 = 0x0100000C
X86_64 = 0x01000007


def name16(name):
    return name.encode().ljust(16, b"\0")


def fixture(cpu=ARM64):
    """One executable section, five symbol classes, UUID and two starts."""
    data = bytearray(0x300)
    struct.pack_into("<8I", data, 0, 0xFEEDFACF, cpu, 0, 2, 4, 216, 0, 0)
    struct.pack_into("<II16sQQQQiiII", data, 32, 0x19, 152, name16("__TEXT"),
                     BASE, 0x1000, 0, len(data), 7, 5, 1, 0)
    struct.pack_into("<16s16sQQ8I", data, 104, name16("__text"), name16("__TEXT"),
                     BASE + 0x100, 0x100, 0x100, 4, 0, 0, 0x80000400, 0, 0, 0)
    struct.pack_into("<II16s", data, 184, 0x1B, 24, bytes(range(16)))
    strings = b"\0_fixture_entry\0_fixture_external\0fixture.c\0absolute\0other\0"
    struct.pack_into("<6I", data, 208, 0x2, 24, 0x200, 5, 0x260, len(strings))
    data[0x260:0x260 + len(strings)] = strings
    symbols = [(1, 0x0F, 1, BASE + 0x100), (16, 0x01, 0, 0),
               (34, 0x64, 0, 0), (44, 0x03, 0, 42), (53, 0x04, 0, 0)]
    # Offsets are calculated from the fixture names so each entry is exact.
    names = [b"_fixture_entry", b"_fixture_external", b"fixture.c", b"absolute", b"other"]
    for index, ((_, kind, section, value), symbol_name) in enumerate(zip(symbols, names)):
        struct.pack_into("<IBBHQ", data, 0x200 + index * 16,
                         strings.index(symbol_name), kind, section, 0, value)
    struct.pack_into("<4I", data, 232, 0x26, 16, 0x2A0, 8)
    data[0x2A0:0x2A8] = b"\x80\x02\x20\0\0\0\0\0"
    return bytes(data)


def patch(data, offset, fmt, *values):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, *values)
    return bytes(result)


def function_data(encoded):
    data = bytearray(fixture())
    struct.pack_into("<I", data, 244, len(encoded))
    data[0x2A0:0x2A0 + len(encoded)] = encoded
    return bytes(data)


def fat_fixture(fat64=False, endian=">"):
    first = fixture(X86_64)
    second = fixture(ARM64)
    data = bytearray(0x2000 + len(second))
    struct.pack_into(endian + "II", data, 0, 0xCAFEBABF if fat64 else 0xCAFEBABE, 2)
    fmt = endian + ("IIQQII" if fat64 else "IIIII")
    for index, (cpu, offset, content) in enumerate(((X86_64, 0x1000, first), (ARM64, 0x2000, second))):
        fields = (cpu, 0, offset, len(content), 12)
        if fat64:
            fields += (0,)
        struct.pack_into(fmt, data, 8 + index * struct.calcsize(fmt), *fields)
        data[offset:offset + len(content)] = content
    return bytes(data)


class InventoryTests(unittest.TestCase):
    def test_thin_metadata_hashes_and_symbol_classes(self):
        data = fixture()
        report = macho.inspect_bytes(data)
        self.assertEqual(report["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(report["format"], "mach-o64")
        item = report["slices"][0]
        self.assertEqual(item["offset"], 0)
        self.assertEqual(item["architecture"], "arm64")
        self.assertEqual(item["uuid"], "00010203-0405-0607-0809-0a0b0c0d0e0f")
        self.assertEqual(item["symbol_table"], {
            "present": True, "total": 5, "defined": 2, "undefined": 1, "stab": 1, "other": 1})
        self.assertTrue(item["no_dwarf"])
        self.assertNotIn("named_text_symbols", item)
        self.assertNotIn("_fixture_entry", json.dumps(report))
        self.assertEqual(item["function_starts"]["addresses"], ["0x100000100", "0x100000120"])
        self.assertEqual(item["function_starts"]["count"], 2)

    def test_optional_names_are_candidates(self):
        item = macho.inspect_bytes(fixture(), True)["slices"][0]
        self.assertEqual(item["named_text_symbols"], [
            {"name": "_fixture_entry", "address": "0x100000100", "section_index": 1}])
        self.assertIn("not proven", item["named_text_symbols_note"])

    def test_fat32_and_fat64_both_byte_orders_use_slice_relative_offsets(self):
        for fat64 in (False, True):
            for endian in (">", "<"):
                with self.subTest(fat64=fat64, endian=endian):
                    data = fat_fixture(fat64, endian)
                    report = macho.inspect_bytes(data, True)
                    self.assertEqual(report["format"], "fat64" if fat64 else "fat32")
                    self.assertEqual(report["sha256"], hashlib.sha256(data).hexdigest())
                    for item, cpu, offset in zip(report["slices"], (X86_64, ARM64), (0x1000, 0x2000)):
                        self.assertEqual(item["offset"], offset)
                        self.assertEqual(item["sha256"], hashlib.sha256(fixture(cpu)).hexdigest())
                        section = item["segments"][0]["sections"][0]
                        self.assertEqual(section["file_offset"], 0x100)
                        self.assertEqual(section["absolute_file_offset"], offset + 0x100)
                        self.assertEqual(item["named_text_symbols"][0]["address"], "0x100000100")
                        self.assertEqual(item["function_starts"]["count"], 2)

    def test_no_dwarf_detects_section_names_and_debug_flag(self):
        for data in (patch(fixture(), 104, "16s", name16("__debug_info")),
                     patch(fixture(), 168, "I", 0x02000000)):
            self.assertFalse(macho.inspect_bytes(data)["slices"][0]["no_dwarf"])

    def test_no_commands_is_valid_and_reports_absent_tables(self):
        data = struct.pack("<8I", 0xFEEDFACF, X86_64, 3, 1, 0, 0, 0, 0)
        item = macho.inspect_bytes(data)["slices"][0]
        self.assertFalse(item["symbol_table"]["present"])
        self.assertFalse(item["function_starts"]["present"])
        self.assertEqual(item["function_starts"]["count"], 0)

    def test_zero_fill_section_does_not_require_file_backing(self):
        data = patch(fixture(), 168, "I", 1)
        data = patch(data, 152, "I", 0xFFFFFFFF)
        section = macho.inspect_bytes(data)["slices"][0]["segments"][0]["sections"][0]
        self.assertTrue(section["zero_fill"])
        self.assertIsNone(section["absolute_file_offset"])

    def test_empty_function_starts_is_valid(self):
        item = macho.inspect_bytes(function_data(b""))["slices"][0]
        self.assertTrue(item["function_starts"]["present"])
        self.assertEqual(item["function_starts"]["count"], 0)

    def test_prebound_undefined_symbol_is_counted_as_undefined(self):
        data = patch(fixture(), 0x210 + 4, "B", 0x0D)
        self.assertEqual(macho.inspect_bytes(data)["slices"][0]["symbol_table"]["undefined"], 1)

    def test_cli_writes_json_once_and_cannot_overwrite_input(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "fixture.macho"
            destination = Path(directory) / "inventory.json"
            source.write_bytes(fixture())
            self.assertEqual(macho.main([str(source), "--output", str(destination)]), 0)
            expected = destination.read_bytes()
            self.assertEqual(json.loads(expected)["format"], "mach-o64")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(macho.main([str(source), "--output", str(destination)]), 2)
                self.assertEqual(macho.main([str(source), "--output", str(source)]), 2)
            self.assertEqual(source.read_bytes(), fixture())
            self.assertEqual(destination.read_bytes(), expected)

    def test_cli_malformed_input_is_reported_without_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "invalid"
            destination = Path(directory) / "never-created.json"
            source.write_bytes(b"bad")
            error = io.StringIO()
            with contextlib.redirect_stderr(error):
                self.assertEqual(macho.main([str(source), "--output", str(destination)]), 2)
            self.assertIn("outside", error.getvalue())
            self.assertFalse(destination.exists())


class MalformedTests(unittest.TestCase):
    def rejects(self, data):
        with self.assertRaises(macho.MachOError):
            macho.inspect_bytes(data, True)

    def test_truncated_and_unsupported_headers(self):
        for data in (b"", b"abc", fixture()[:31], patch(fixture(), 0, "I", 0xFEEDFACE),
                     patch(fixture(), 4, "I", 7)):
            with self.subTest(size=len(data)):
                self.rejects(data)

    def test_load_command_bounds_sizes_and_counts(self):
        changes = [(20, "I", 0xFFFFFFFF), (16, "I", 0xFFFFFFFF),
                   (36, "I", 0), (36, "I", 150), (36, "I", 224),
                   (16, "I", 3), (20, "I", 224), (96, "I", 0xFFFFFFFF)]
        for offset, fmt, value in changes:
            with self.subTest(offset=offset, value=value):
                self.rejects(patch(fixture(), offset, fmt, value))

    def test_segment_and_section_ranges_and_address_overflow(self):
        changes = [(72, "Q", (1 << 64) - 1), (80, "Q", (1 << 64) - 1),
                   (56, "Q", (1 << 64) - 1), (136, "Q", (1 << 64) - 1),
                   (144, "Q", (1 << 64) - 1), (152, "I", 0x2F0),
                   (156, "I", 64), (160, "I", 0x2FF), (164, "I", 0xFFFFFFFF)]
        for offset, fmt, value in changes:
            with self.subTest(offset=offset, value=value):
                data = patch(fixture(), offset, fmt, value)
                if offset == 160:
                    data = patch(data, 164, "I", 1)
                self.rejects(data)

    def test_section_must_lie_in_declared_segment_file_range(self):
        self.rejects(patch(fixture(), 80, "Q", 0x180))

    def test_symbol_and_string_table_bounds(self):
        for offset, value in ((216, 0x2FF), (220, 0xFFFFFFFF), (224, 0x2FF),
                              (228, 0xFFFFFFFF), (0x200, 0xFFFF)):
            with self.subTest(offset=offset):
                self.rejects(patch(fixture(), offset, "I", value))
        self.rejects(patch(fixture(), 0x205, "B", 2))
        data = bytearray(fixture())
        strsize = struct.unpack_from("<I", data, 228)[0]
        data[0x260 + strsize - 1] = 1
        self.rejects(bytes(data))

    def test_function_start_bounds_and_malformed_uleb(self):
        self.rejects(patch(fixture(), 240, "I", 0x2FF))
        for encoded in (b"\x80", b"\x80" * 9 + b"\x02", b"\x80" * 10 + b"\0",
                        b"\xff" * 9 + b"\x01\0", b"\1", b"\1\0\2", b"\x80\x20\0"):
            with self.subTest(encoded=encoded):
                self.rejects(function_data(encoded))
        self.rejects(patch(fixture(), 40, "16s", name16("__OTHER")))
        self.rejects(patch(fixture(), 92, "i", 1))  # no executable segment

    def test_fat_table_truncation_and_empty_count(self):
        for data in (fat_fixture()[:7], fat_fixture()[:30],
                     patch(fat_fixture(), 4, ">I", 0),
                     patch(fat_fixture(), 4, ">I", 0xFFFFFFFF)):
            self.rejects(data)

    def test_fat64_offsets_and_sizes_cannot_wrap(self):
        for offset in (16, 24):
            self.rejects(patch(fat_fixture(True), offset, ">Q", (1 << 64) - 1))

    def test_fat_slices_cannot_overlap_header_or_each_other(self):
        self.rejects(patch(fat_fixture(), 16, ">I", 0))
        self.rejects(patch(fat_fixture(), 36, ">I", 0x1000))

    def test_fat_alignment_and_cpu_metadata(self):
        self.rejects(patch(fat_fixture(), 24, ">I", 64))
        self.rejects(patch(fat_fixture(), 16, ">I", 0x1001))
        self.rejects(patch(fat_fixture(), 8, ">I", ARM64))
        self.rejects(patch(fat_fixture(), 12, ">I", 1))


if __name__ == "__main__":
    unittest.main()
