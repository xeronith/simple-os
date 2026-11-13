#!/usr/bin/env python3
"""peify.py - link simple-os UEFI objects into a PE32+ EFI application.

The objects are ordinary AArch64 ELF relocatables produced by clang. This tool
lays code and read-only data into a PE .text section, writable data and BSS into
a .data section, records the absolute addresses that need base relocations, and
emits a single BOOTAA64.EFI image.

It reuses the ELF parser and instruction patching from elflink.py.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import elflink  # noqa: E402
from elflink import (  # noqa: E402
    SHN_ABS,
    SHN_COMMON,
    SHN_UNDEF,
    SHT_NOBITS,
    SHT_RELA,
    ObjectFile,
    align_up,
    die,
)

SHF_WRITE = 0x1

# PE constants.
IMAGE_FILE_MACHINE_ARM64 = 0xAA64
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002
IMAGE_FILE_LARGE_ADDRESS_AWARE = 0x0020
IMAGE_SUBSYSTEM_EFI_APPLICATION = 10
IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000

IMAGE_REL_BASED_DIR64 = 10

R_AARCH64_ABS64 = 257
R_AARCH64_ABS32 = 258


def build_base_relocs(entries: list[tuple[int, int]]) -> bytes:
    """Build a PE .reloc section from (RVA, type) entries."""
    pages: dict[int, list[int]] = {}
    for rva, kind in sorted(set(entries)):
        pages.setdefault(rva & ~0xFFF, []).append((kind << 12) | (rva & 0xFFF))

    output = bytearray()
    for page in sorted(pages):
        block = pages[page]
        block_size = 8 + 2 * len(block)
        if block_size % 4:
            block.append(0)  # padding entry: type 0, offset 0
            block_size += 2
        output += struct.pack("<II", page, block_size)
        for entry in block:
            output += struct.pack("<H", entry)
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description="link a UEFI AArch64 PE32+ image")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("-e", "--entry", default="efi_main")
    parser.add_argument("-b", "--image-base", default="0x0")
    parser.add_argument("-q", "--quiet", action="store_true")
    parser.add_argument("objects", nargs="+")
    args = parser.parse_args()

    image_base = int(args.image_base, 0)
    text_rva = 0x1000
    file_align = 0x200
    section_align = 0x1000
    headers_size = 0x200

    objects = [ObjectFile(path) for path in args.objects]

    # Split sections by writability. Code and rodata go to .text (RX); mutable
    # data and BSS go to .data (RW). UEFI maps code read-only even when a single
    # section asks for write access, so the split matters.
    text_secs = []
    data_secs = []
    for obj in objects:
        for sec in obj.sections:
            if not sec.is_alloc or not sec.size:
                continue
            if (sec.flags & SHF_WRITE) or sec.sh_type == SHT_NOBITS:
                data_secs.append(sec)
            else:
                text_secs.append(sec)

    cursor = text_rva
    for sec in text_secs:
        cursor = align_up(cursor, max(sec.align, 1))
        sec.out_addr = image_base + cursor
        cursor += sec.size
    text_end = align_up(cursor, 16)
    text_size = text_end - text_rva

    data_rva = align_up(text_end, section_align)
    cursor = data_rva
    for sec in data_secs:
        cursor = align_up(cursor, max(sec.align, 1))
        sec.out_addr = image_base + cursor
        cursor += sec.size
    data_end = align_up(cursor, 16)
    data_size = data_end - data_rva

    # Everything is file-backed, including BSS, so all data can be initialized
    # with zeroes. The image is one contiguous RVA buffer for relocation.
    image = bytearray(max(data_end, 1))
    for sec in text_secs + data_secs:
        if sec.sh_type != SHT_NOBITS:
            start = sec.out_addr - image_base
            image[start : start + sec.size] = sec.data

    specials = {
        "__image_start": image_base,
        "__image_end": image_base + data_end,
        "__bss_start": image_base + data_rva,
        "__bss_end": image_base + data_end,
        "__kernel_end": image_base + data_end,
    }

    globals_map: dict[str, int] = {}
    for obj in objects:
        for sym in (s for table in obj.symtabs.values() for s in table):
            if not sym.is_global or sym.shndx == SHN_UNDEF or not sym.name:
                continue
            if sym.shndx == SHN_ABS:
                globals_map[sym.name] = sym.value
            elif sym.shndx == SHN_COMMON:
                die(f"{obj.path}: common symbol {sym.name!r}; compile with -fno-common")
            else:
                sec = obj.by_index.get(sym.shndx)
                if sec and sec.is_alloc:
                    globals_map[sym.name] = sec.out_addr + sym.value

    entry = globals_map.get(args.entry)
    if entry is None:
        die(f"entry symbol {args.entry!r} not found")
    entry_rva = entry - image_base
    if not (text_rva <= entry_rva < text_end):
        die(f"entry symbol {args.entry!r} is not inside the text section")

    base_relocs: list[tuple[int, int]] = []

    for obj in objects:
        for rsec in obj.sections:
            if rsec.sh_type != SHT_RELA:
                continue
            target = obj.by_index.get(rsec.info)
            if target is None or not target.is_alloc:
                continue
            symtab = obj.symtabs.get(rsec.link)
            if symtab is None:
                die(f"{obj.path}: relocation section {rsec.name} has no symbol table")
            where = f"{obj.path}:{rsec.name}+0x{rsec.offset:x}"

            for off in range(rsec.offset, rsec.offset + rsec.size, 24):
                r_offset, r_info, r_addend = struct.unpack_from("<QQq", obj.blob, off)
                sym_index = r_info >> 32
                rtype = r_info & 0xFFFFFFFF
                if sym_index >= len(symtab):
                    die(f"{obj.path}: bad symbol index {sym_index}")
                sym = symtab[sym_index]

                if sym.shndx == SHN_UNDEF:
                    addr = globals_map.get(sym.name)
                    if addr is None:
                        addr = specials.get(sym.name)
                    if addr is None:
                        die(f"undefined symbol {sym.name!r} referenced by {obj.path}")
                elif sym.shndx == SHN_ABS:
                    addr = sym.value
                elif sym.shndx == SHN_COMMON:
                    die(f"{obj.path}: common symbol {sym.name!r}; compile with -fno-common")
                else:
                    sec = obj.by_index.get(sym.shndx)
                    if sec is None or not sec.is_alloc:
                        die(f"{obj.path}: symbol {sym.name!r} in non-allocatable section")
                    addr = sec.out_addr + sym.value

                if rtype == R_AARCH64_ABS64:
                    p = target.out_addr + r_offset
                    image_off = p - image_base
                    if image_off < 0 or image_off + 8 > len(image):
                        die(f"{where}: absolute relocation outside image")
                    struct.pack_into("<Q", image, image_off,
                                     (addr + r_addend) & 0xFFFFFFFFFFFFFFFF)
                    base_relocs.append((p - image_base, IMAGE_REL_BASED_DIR64))
                elif rtype == R_AARCH64_ABS32:
                    die(f"{where}: 32-bit absolute relocation is not supported; use 64-bit addresses")
                else:
                    elflink.apply_reloc(
                        image,
                        image_base,
                        target,
                        r_offset,
                        rtype,
                        sym,
                        r_addend,
                        addr,
                        where,
                    )

    reloc_data = build_base_relocs(base_relocs)
    reloc_rva = align_up(data_end, section_align)
    text_raw_size = align_up(text_size, file_align)
    data_raw_size = align_up(data_size, file_align)
    reloc_raw_size = align_up(len(reloc_data), file_align)
    text_file_offset = headers_size
    data_file_offset = text_file_offset + text_raw_size
    reloc_file_offset = data_file_offset + data_raw_size
    end_rva = reloc_rva + len(reloc_data) if reloc_data else data_end
    size_of_image = align_up(end_rva, section_align)

    data_dirs = bytearray(16 * 8)
    if reloc_data:
        struct.pack_into("<II", data_dirs, 5 * 8, reloc_rva, len(reloc_data))

    optional = struct.pack(
        "<HBBIIIIIQIIHHHHHHIIIIHHQQQQII",
        0x20B,  # PE32+
        0,
        0,
        text_raw_size,
        data_raw_size + reloc_raw_size,
        0,
        entry_rva,
        text_rva,
        image_base,
        section_align,
        file_align,
        0, 0, 0, 0, 0, 0, 0,
        size_of_image,
        headers_size,
        0,
        IMAGE_SUBSYSTEM_EFI_APPLICATION,
        0,
        0x100000,
        0x1000,
        0x100000,
        0x1000,
        0,
        16,
    ) + bytes(data_dirs)
    if len(optional) != 0xF0:
        die(f"internal error: optional header is {len(optional)} bytes")

    sections = [
        (
            b".text\0\0\0",
            text_size,
            text_rva,
            text_raw_size,
            text_file_offset,
            IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ,
        )
    ]
    if data_size:
        sections.append(
            (
                b".data\0\0\0",
                data_size,
                data_rva,
                data_raw_size,
                data_file_offset,
                IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                IMAGE_SCN_MEM_WRITE,
            )
        )
    if reloc_data:
        sections.append(
            (
                b".reloc\0\0",
                len(reloc_data),
                reloc_rva,
                reloc_raw_size,
                reloc_file_offset,
                IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                IMAGE_SCN_MEM_DISCARDABLE,
            )
        )

    section_headers = bytearray()
    for name, virtual_size, virtual_address, raw_size, raw_offset, characteristics in sections:
        section_headers += struct.pack(
            "<8sIIIIIIHHI",
            name,
            virtual_size,
            virtual_address,
            raw_size,
            raw_offset,
            0,
            0,
            0,
            0,
            characteristics,
        )

    header = bytearray(headers_size)
    struct.pack_into("<H", header, 0, 0x5A4D)  # MZ
    struct.pack_into("<I", header, 0x3C, 0x40)  # e_lfanew
    header[0x40:0x44] = b"PE\0\0"
    struct.pack_into(
        "<HHIIIHH",
        header,
        0x44,
        IMAGE_FILE_MACHINE_ARM64,
        len(sections),
        0,
        0,
        0,
        0xF0,
        IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE,
    )
    header[0x58 : 0x58 + len(optional)] = optional
    header[0x148 : 0x148 + len(section_headers)] = section_headers
    if 0x148 + len(section_headers) > headers_size:
        die("internal error: headers do not fit")

    output = bytearray(header)
    output += image[text_rva:text_rva + text_size]
    output += b"\0" * (text_raw_size - text_size)
    if data_size:
        output += image[data_rva:data_rva + data_size]
        output += b"\0" * (data_raw_size - data_size)
    if reloc_data:
        output += reloc_data
        output += b"\0" * (reloc_raw_size - len(reloc_data))

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_bytes(output)

    if not args.quiet:
        print(f"linked {args.output}")
        print(f"  image   0x{image_base:016x} - 0x{image_base + data_end:016x}")
        print(f"  entry   0x{entry:016x} (RVA 0x{entry_rva:x}, {args.entry})")
        print(f"  base relocations: {len(base_relocs)}")
        print(f"  file    {len(output)} bytes")


if __name__ == "__main__":
    main()
