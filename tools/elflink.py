#!/usr/bin/env python3
"""elflink.py - the small linker used by simple-os.

Clang can emit bare-metal AArch64 ELF objects, but not every clang
installation ships a matching bare-metal linker. This script supplies the
missing link step: it merges the allocatable sections of one or more
relocatable ELF64/AArch64 objects, applies the handful of relocation types the
kernel emits, and writes a one-segment ELF executable that QEMU's "-kernel"
loader understands.

This is deliberately a toy linker, not a general-purpose replacement for
ld.lld or a GNU cross-linker. Unsupported relocations are reported with their
number and symbol name.
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHF_ALLOC = 0x2
SHN_UNDEF = 0
SHN_ABS = 0xFFF1
SHN_COMMON = 0xFFF2

EM_AARCH64 = 183

# AArch64 relocation types we know how to apply.
RELOC_NAMES = {
    0: "R_AARCH64_NONE",
    257: "R_AARCH64_ABS64",
    258: "R_AARCH64_ABS32",
    259: "R_AARCH64_ABS16",
    260: "R_AARCH64_PREL64",
    261: "R_AARCH64_PREL32",
    262: "R_AARCH64_PREL16",
    263: "R_AARCH64_MOVW_UABS_G0",
    264: "R_AARCH64_MOVW_UABS_G0_NC",
    265: "R_AARCH64_MOVW_UABS_G1",
    266: "R_AARCH64_MOVW_UABS_G1_NC",
    267: "R_AARCH64_MOVW_UABS_G2",
    268: "R_AARCH64_MOVW_UABS_G2_NC",
    269: "R_AARCH64_MOVW_UABS_G3",
    270: "R_AARCH64_MOVW_SABS_G0",
    271: "R_AARCH64_MOVW_SABS_G1",
    272: "R_AARCH64_MOVW_SABS_G2",
    273: "R_AARCH64_LD_PREL_LO19",
    274: "R_AARCH64_ADR_PREL_LO21",
    275: "R_AARCH64_ADR_PREL_PG_HI21",
    276: "R_AARCH64_ADR_PREL_PG_HI21_NC",
    277: "R_AARCH64_ADD_ABS_LO12_NC",
    278: "R_AARCH64_LDST8_ABS_LO12_NC",
    279: "R_AARCH64_TSTBR14",
    280: "R_AARCH64_CONDBR19",
    282: "R_AARCH64_JUMP26",
    283: "R_AARCH64_CALL26",
    284: "R_AARCH64_LDST16_ABS_LO12_NC",
    285: "R_AARCH64_LDST32_ABS_LO12_NC",
    286: "R_AARCH64_LDST64_ABS_LO12_NC",
    299: "R_AARCH64_LDST128_ABS_LO12_NC",
    311: "R_AARCH64_ADR_GOT_PAGE",
    312: "R_AARCH64_LD64_GOT_LO12_NC",
    313: "R_AARCH64_LD64_GOTPAGE_LO15",
}


def die(msg: str) -> None:
    print(f"elflink: error: {msg}", file=sys.stderr)
    raise SystemExit(1)


def align_up(value: int, alignment: int) -> int:
    if alignment <= 1:
        return value
    return (value + alignment - 1) & ~(alignment - 1)


@dataclass
class Section:
    obj: "ObjectFile"
    index: int
    name: str
    sh_type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    entsize: int
    out_addr: int = 0

    @property
    def is_alloc(self) -> bool:
        return bool(self.flags & SHF_ALLOC)

    @property
    def data(self) -> bytes:
        return self.obj.blob[self.offset : self.offset + self.size]


@dataclass
class Symbol:
    name: str
    info: int
    other: int
    shndx: int
    value: int
    size: int

    @property
    def is_global(self) -> bool:
        return (self.info >> 4) in (1, 2)  # GLOBAL, WEAK


class ObjectFile:
    def __init__(self, path: str):
        self.path = Path(path)
        self.blob = self.path.read_bytes()
        if len(self.blob) < 64 or self.blob[:4] != b"\x7fELF":
            die(f"{self.path}: not an ELF file")
        if self.blob[4] != 2 or self.blob[5] != 1:
            die(f"{self.path}: expected ELF64 little-endian")
        fields = struct.unpack_from("<HHIQQQIHHHHHH", self.blob, 16)
        (
            self.e_type,
            self.e_machine,
            self.e_version,
            self.e_entry,
            self.e_phoff,
            self.e_shoff,
            self.e_flags,
            self.e_ehsize,
            self.e_phentsize,
            self.e_phnum,
            self.e_shentsize,
            self.e_shnum,
            self.e_shstrndx,
        ) = fields
        if self.e_machine != EM_AARCH64:
            die(f"{self.path}: not an AArch64 object (e_machine={self.e_machine})")

        raw = []
        for i in range(self.e_shnum):
            off = self.e_shoff + i * self.e_shentsize
            raw.append(struct.unpack_from("<IIQQQQIIQQ", self.blob, off))

        shstr = raw[self.e_shstrndx]
        shstr_data = self.blob[shstr[4] : shstr[4] + shstr[5]]

        self.sections: list[Section] = []
        self.by_index: dict[int, Section] = {}
        for i, vals in enumerate(raw):
            name_off, sh_type, flags, addr, offset, size, link, info, align, entsize = vals
            end = shstr_data.find(b"\0", name_off)
            name = shstr_data[name_off:end].decode("utf-8", "replace")
            sec = Section(
                self, i, name, sh_type, flags, addr, offset, size, link, info, align, entsize
            )
            self.sections.append(sec)
            self.by_index[i] = sec

        # Parse every symbol table, keyed by the section index of the table.
        self.symtabs: dict[int, list[Symbol]] = {}
        for sec in self.sections:
            if sec.sh_type != SHT_SYMTAB:
                continue
            strtab = self.by_index[sec.link]
            strings = strtab.data
            syms: list[Symbol] = []
            count = sec.size // 24
            for i in range(count):
                off = sec.offset + i * 24
                st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                    "<IBBHQQ", self.blob, off
                )
                end = strings.find(b"\0", st_name)
                name = strings[st_name:end].decode("utf-8", "replace")
                syms.append(Symbol(name, st_info, st_other, st_shndx, st_value, st_size))
            self.symtabs[sec.index] = syms


def read_u32(buf: bytearray, off: int, ctx: str) -> int:
    if off < 0 or off + 4 > len(buf):
        die(f"relocation patch outside image ({ctx})")
    return struct.unpack_from("<I", buf, off)[0]


def write_u32(buf: bytearray, off: int, value: int, ctx: str) -> None:
    if off < 0 or off + 4 > len(buf):
        die(f"relocation patch outside image ({ctx})")
    struct.pack_into("<I", buf, off, value & 0xFFFFFFFF)


def signed_ok(value: int, bits: int) -> bool:
    return -(1 << (bits - 1)) <= value < (1 << (bits - 1))


def patch_imm(insn: int, value: int, shift: int, width: int) -> int:
    mask = ((1 << width) - 1) << shift
    return (insn & ~mask) | ((value & ((1 << width) - 1)) << shift)


def patch_adr(insn: int, imm21: int) -> int:
    insn &= ~((0x3 << 29) | (0x7FFFF << 5))
    insn |= (imm21 & 0x3) << 29
    insn |= ((imm21 >> 2) & 0x7FFFF) << 5
    return insn


def apply_reloc(
    buf: bytearray,
    image_base: int,
    target: Section,
    r_offset: int,
    rtype: int,
    symbol: Symbol,
    addend: int,
    s_value: int,
    where: str,
) -> None:
    p = target.out_addr + r_offset
    off = p - image_base
    name = RELOC_NAMES.get(rtype, f"unknown relocation {rtype}")
    ctx = f"{where}: {name} against {symbol.name or '<section>'}"

    if rtype == 0:
        return

    if rtype == 257:  # ABS64
        if off < 0 or off + 8 > len(buf):
            die(f"{ctx}: target outside image")
        struct.pack_into("<Q", buf, off, (s_value + addend) & 0xFFFFFFFFFFFFFFFF)
        return

    if rtype in (258, 259):
        width = 4 if rtype == 258 else 2
        if off < 0 or off + width > len(buf):
            die(f"{ctx}: target outside image")
        struct.pack_into("<I" if width == 4 else "<H", buf, off, (s_value + addend) & ((1 << (width * 8)) - 1))
        return

    if rtype == 260:  # PREL64
        if off < 0 or off + 8 > len(buf):
            die(f"{ctx}: target outside image")
        struct.pack_into("<Q", buf, off, (s_value + addend - p) & 0xFFFFFFFFFFFFFFFF)
        return

    if rtype == 261:  # PREL32
        write_u32(buf, off, s_value + addend - p, ctx)
        return

    insn = read_u32(buf, off, ctx)

    if rtype == 273:  # LD_PREL_LO19
        delta = s_value + addend - p
        if delta & 3 or not signed_ok(delta >> 2, 19):
            die(f"{ctx}: out of range")
        insn = patch_imm(insn, delta >> 2, 5, 19)
    elif rtype == 274:  # ADR_PREL_LO21
        delta = s_value + addend - p
        if not signed_ok(delta, 21):
            die(f"{ctx}: out of range")
        insn = patch_adr(insn, delta)
    elif rtype in (275, 276):  # ADR_PREL_PG_HI21
        delta = ((s_value + addend) & ~0xFFF) - (p & ~0xFFF)
        imm = delta >> 12
        if not signed_ok(imm, 21):
            die(f"{ctx}: out of range")
        insn = patch_adr(insn, imm)
    elif rtype == 277:  # ADD_ABS_LO12_NC
        insn = patch_imm(insn, (s_value + addend) & 0xFFF, 10, 12)
    elif rtype in (278, 284, 285, 286, 299):  # LDST{8,16,32,64,128}_ABS_LO12_NC
        shift = {278: 0, 284: 1, 285: 2, 286: 3, 299: 4}[rtype]
        insn = patch_imm(insn, ((s_value + addend) & 0xFFF) >> shift, 10, 12)
    elif rtype in (263, 264, 265, 266, 267, 268, 269, 270, 271, 272):
        value = s_value + addend
        if rtype in (263, 264, 270):
            imm = value & 0xFFFF
        elif rtype in (265, 266, 271):
            imm = (value >> 16) & 0xFFFF
        elif rtype in (267, 268, 272):
            imm = (value >> 32) & 0xFFFF
        else:
            imm = (value >> 48) & 0xFFFF
        insn = patch_imm(insn, imm, 5, 16)
    elif rtype == 279:  # TSTBR14
        delta = s_value + addend - p
        if delta & 3 or not signed_ok(delta >> 2, 14):
            die(f"{ctx}: out of range")
        insn = patch_imm(insn, delta >> 2, 5, 14)
    elif rtype == 280:  # CONDBR19
        delta = s_value + addend - p
        if delta & 3 or not signed_ok(delta >> 2, 19):
            die(f"{ctx}: out of range")
        insn = patch_imm(insn, delta >> 2, 5, 19)
    elif rtype in (282, 283):  # JUMP26, CALL26
        delta = s_value + addend - p
        if delta & 3 or not signed_ok(delta >> 2, 26):
            die(f"{ctx}: out of range")
        insn = (insn & 0xFC000000) | ((delta >> 2) & 0x3FFFFFF)
    elif rtype in (311, 312, 313):
        die(f"{ctx}: GOT relocations are not supported (compile with -fno-pic)")
    else:
        die(f"{ctx}: unsupported relocation type {rtype}")

    write_u32(buf, off, insn, ctx)


def main() -> None:
    ap = argparse.ArgumentParser(description="link AArch64 ELF objects for simple-os")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("-b", "--base", default="0x40200000", help="physical load address")
    ap.add_argument("-e", "--entry", default="_start", help="entry symbol")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("objects", nargs="+")
    args = ap.parse_args()

    image_base = int(args.base, 0)
    objects = [ObjectFile(p) for p in args.objects]

    # Place all file-backed allocatable sections first, then .bss.
    file_secs: list[Section] = []
    bss_secs: list[Section] = []
    for obj in objects:
        for sec in obj.sections:
            if not sec.is_alloc:
                continue
            if sec.sh_type == SHT_NOBITS:
                if sec.size:
                    bss_secs.append(sec)
            elif sec.size:
                file_secs.append(sec)

    cursor = image_base
    for sec in file_secs:
        cursor = align_up(cursor, max(sec.align, 1))
        sec.out_addr = cursor
        cursor += sec.size
    data_end = cursor

    cursor = align_up(cursor, 16)
    bss_start = cursor
    for sec in bss_secs:
        cursor = align_up(cursor, max(sec.align, 1))
        sec.out_addr = cursor
        cursor += sec.size
    bss_end = align_up(cursor, 16)

    buf = bytearray(data_end - image_base)
    for sec in file_secs:
        start = sec.out_addr - image_base
        buf[start : start + sec.size] = sec.data

    specials = {
        "__image_start": image_base,
        "__image_end": data_end,
        "__bss_start": bss_start,
        "__bss_end": bss_end,
        "__kernel_end": bss_end,
    }

    # Resolve global symbols once all section addresses are known.
    globals_map: dict[str, int] = {}
    for obj in objects:
        for sym in (s for tab in obj.symtabs.values() for s in tab):
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

    missing = object()
    entry = globals_map.get(args.entry, missing)
    if entry is missing:
        die(f"entry symbol {args.entry!r} not found")

    for obj in objects:
        for rsec in obj.sections:
            if rsec.sh_type != SHT_RELA:
                continue
            target = obj.by_index.get(rsec.info)
            if target is None or not target.is_alloc:
                continue
            if target.sh_type == SHT_NOBITS:
                die(f"{obj.path}: relocation against bss section {target.name}")
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

                apply_reloc(buf, image_base, target, r_offset, rtype, sym, r_addend, addr, where)

    # Section headers are not needed by QEMU, but they make the image readable
    # with objdump, readelf and gdb.
    shstr = bytearray(b"\0")
    name_offsets: dict[str, int] = {}

    def add_name(name: str) -> int:
        if name not in name_offsets:
            name_offsets[name] = len(shstr)
            shstr.extend(name.encode() + b"\0")
        return name_offsets[name]

    shdrs = [struct.pack("<IIQQQQIIQQ", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    for sec in file_secs + bss_secs:
        name_off = add_name(sec.name)
        if sec.sh_type == SHT_NOBITS:
            file_offset = 0x1000 + len(buf)
        else:
            file_offset = 0x1000 + (sec.out_addr - image_base)
        shdrs.append(
            struct.pack(
                "<IIQQQQIIQQ",
                name_off,
                sec.sh_type,
                sec.flags,
                sec.out_addr,
                file_offset,
                sec.size,
                0,
                0,
                sec.align,
                0,
            )
        )

    shstr_name = add_name(".shstrtab")
    shstr_offset = 0x1000 + len(buf)
    shdrs.append(
        struct.pack(
            "<IIQQQQIIQQ",
            shstr_name,
            3,  # SHT_STRTAB
            0,
            0,
            shstr_offset,
            len(shstr),
            0,
            0,
            1,
            0,
        )
    )

    shoff = align_up(shstr_offset + len(shstr), 8)
    shstrndx = len(shdrs) - 1

    ident = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        ident,
        2,  # ET_EXEC
        EM_AARCH64,
        1,
        entry,
        64,  # e_phoff
        shoff,
        0,  # e_flags
        64,  # e_ehsize
        56,  # e_phentsize
        1,  # e_phnum
        64,  # e_shentsize
        len(shdrs),
        shstrndx,
    )
    phdr = struct.pack(
        "<IIQQQQQQ",
        1,  # PT_LOAD
        7,  # RWX
        0x1000,  # p_offset
        image_base,
        image_base,
        len(buf),
        bss_end - image_base,
        0x1000,
    )

    out = bytearray(0x1000)
    out[0:64] = ehdr
    out[64:120] = phdr
    out += buf
    out += b"\0" * (shstr_offset - len(out))
    out += shstr
    out += b"\0" * (shoff - len(out))
    for shdr in shdrs:
        out += shdr

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_bytes(out)

    if not args.quiet:
        print(f"linked {args.output}")
        print(f"  load   0x{image_base:016x} - 0x{data_end:016x}")
        print(f"  bss    0x{bss_start:016x} - 0x{bss_end:016x}")
        print(f"  entry  0x{entry:016x} ({args.entry})")


if __name__ == "__main__":
    main()
