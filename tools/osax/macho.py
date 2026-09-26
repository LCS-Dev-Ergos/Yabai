#!/usr/bin/env python3
"""Static analysis helpers for locating scripting-addition targets in Dock.app.

Reads fat or thin arm64 Mach-O files with the standard library only. Pointers
are decoded from chained fixups on a best-effort basis, which is enough for
the Objective-C metadata in Dock.

  macho.py slices   BIN               list the slices of a fat binary
  macho.py extract  BIN DIR           write each slice to DIR/<name>.<index>
  macho.py sections BIN               list sections
  macho.py methods  BIN               list Objective-C methods with their IMP
  macho.py ivars    BIN [CLASS ...]   list instance variables with offsets
  macho.py annotate BIN < LISTING     tag objc_msgSend stub calls with selectors
  macho.py callers  REGEX < LISTING   list functions containing matching calls

BIN commands take --slice N to pick a slice of a fat binary (default 0).
LISTING is the output of: llvm-objdump --macho -d <thin slice>
"""

import argparse
import re
import struct
import sys
from pathlib import Path

FAT_MAGIC = 0xCAFEBABE
MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
CPU_TYPE_ARM64 = 0x0100000C
BASE = 0x100000000

# pacibsp, and pacibsppc as used by the arm64e.x1 slice since macOS 27.
PROLOGUES = ("\tpacibsp", "\t7f 23 03 d5", "\tfe a7 c1 da")


def fat_slices(data):
    """Return [(cputype, cpusubtype, offset, size)], or one entry for thin files."""
    if struct.unpack_from(">I", data, 0)[0] != FAT_MAGIC:
        cputype, cpusubtype = struct.unpack_from("<iI", data, 4)
        return [(cputype, cpusubtype & 0xFFFFFF, 0, len(data))]
    count = struct.unpack_from(">I", data, 4)[0]
    return [
        (ct, cs & 0xFFFFFF, off, size)
        for ct, cs, off, size, _ in (
            struct.unpack_from(">iIIII", data, 8 + i * 20) for i in range(count)
        )
    ]


class MachO:
    def __init__(self, path, slice_index=0):
        data = Path(path).read_bytes()
        _, _, off, size = fat_slices(data)[slice_index]
        self.d = data[off : off + size]
        magic, _, _, _, ncmds = struct.unpack_from("<IiiII", self.d, 0)
        if magic != MH_MAGIC_64:
            raise SystemExit(f"{path}: slice {slice_index} is not a 64-bit Mach-O")
        self.sections = []
        self.segments = []
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from("<II", self.d, off)
            if cmd == LC_SEGMENT_64:
                segname = self.d[off + 8 : off + 24].rstrip(b"\0").decode()
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from(
                    "<QQQQ", self.d, off + 24
                )
                nsects = struct.unpack_from("<I", self.d, off + 64)[0]
                self.segments.append((segname, vmaddr, vmsize, fileoff, filesize))
                so = off + 72
                for _ in range(nsects):
                    sect = self.d[so : so + 16].rstrip(b"\0").decode()
                    seg = self.d[so + 16 : so + 32].rstrip(b"\0").decode()
                    addr, size = struct.unpack_from("<QQ", self.d, so + 32)
                    self.sections.append((seg, sect, addr, size))
                    so += 80
            off += cmdsize

    def section(self, name):
        return next((s for s in self.sections if s[1] == name), None)

    def foff(self, va):
        for _, vmaddr, _, fileoff, filesize in self.segments:
            if vmaddr <= va < vmaddr + filesize:
                return va - vmaddr + fileoff
        return None

    def at(self, va):
        """File offset of va; raises for addresses without file backing."""
        o = self.foff(va)
        if o is None:
            raise ValueError(f"{va:#x} is not backed by the file")
        return o

    def u32(self, va):
        return struct.unpack_from("<I", self.d, self.at(va))[0]

    def s32(self, va):
        return struct.unpack_from("<i", self.d, self.at(va))[0]

    def ptr(self, va):
        """Resolve a chained-fixup rebase to a vmaddr; binds and nulls give None."""
        if va is None or self.foff(va) is None:
            return None
        v = struct.unpack_from("<Q", self.d, self.at(va))[0]
        if v == 0 or v >> 62 & 1:
            return None
        target = (v & 0xFFFFFFFF) if v >> 63 else (v & 0x7FFFFFFFFFF)
        return target + BASE if target < BASE else target

    def cstr(self, va):
        o = self.foff(va) if va is not None else None
        if o is None:
            return "?"
        return self.d[o : self.d.index(b"\0", o)].decode(errors="replace")

    @staticmethod
    def adrp(insn, pc):
        imm = ((insn >> 5) & 0x7FFFF) << 2 | (insn >> 29) & 3
        if imm & (1 << 20):
            imm -= 1 << 21
        return (pc & ~0xFFF) + (imm << 12)

    def objc_stubs(self):
        """Map each __objc_stubs entry to the selector it sends."""
        out = {}
        s = self.section("__objc_stubs")
        if not s:
            return out
        for va in range(s[2], s[2] + s[3], 32):
            i0, i1 = self.u32(va), self.u32(va + 4)
            if (i0 & 0x9F000000) == 0x90000000 and (i1 & 0xFFC00000) == 0xF9400000:
                selref = self.adrp(i0, va) + ((i1 >> 10) & 0xFFF) * 8
                out[va] = self.cstr(self.ptr(selref))
        return out

    def method_list(self, ml):
        if not ml or self.foff(ml) is None:
            return
        flags, count = self.u32(ml), self.u32(ml + 4)
        entsize = flags & 0xFFFC
        for i in range(count):
            e = ml + 8 + i * entsize
            if flags & 0x80000000:  # relative method list
                yield self.cstr(self.ptr(e + self.s32(e))), e + 8 + self.s32(e + 8)
            else:
                yield self.cstr(self.ptr(e)), self.ptr(e + 16) or 0

    def classes(self):
        """Yield (name, class_ro) for every class and metaclass."""
        s = self.section("__objc_classlist")
        if not s:
            return
        for i in range(s[3] // 8):
            cls = self.ptr(s[2] + i * 8)
            for c in (cls, self.ptr(cls) if cls else None):
                ro = self.ptr(c + 32) if c else None
                if ro:
                    ro &= ~7
                    yield self.cstr(self.ptr(ro + 24)), ro, c is not cls

    def methods(self):
        """Yield (class, is_meta, selector, imp) for classes and categories."""
        for name, ro, meta in self.classes():
            for sel, imp in self.method_list(self.ptr(ro + 32)):
                yield name, meta, sel, imp
        s = self.section("__objc_catlist")
        if not s:
            return
        for i in range(s[3] // 8):
            cat = self.ptr(s[2] + i * 8)
            if cat is None:
                continue
            name = "category:" + self.cstr(self.ptr(cat))
            for meta, off in ((False, 16), (True, 24)):
                for sel, imp in self.method_list(self.ptr(cat + off)):
                    yield name, meta, sel, imp

    def ivars(self):
        """Yield (class, ivar, type, offset, size); offset is -1 if unresolved."""
        for name, ro, meta in self.classes():
            il = None if meta else self.ptr(ro + 48)
            if not il or self.foff(il) is None:
                continue
            entsize, count = self.u32(il), self.u32(il + 4)
            for j in range(count):
                e = il + 8 + j * entsize
                offp = self.ptr(e)
                offset = self.u32(offp) if offp and self.foff(offp) is not None else -1
                yield (
                    name,
                    self.cstr(self.ptr(e + 8)),
                    self.cstr(self.ptr(e + 16)),
                    offset,
                    self.u32(e + 28),
                )


def cmd_callers(pattern, listing):
    rx = re.compile(pattern)
    function, seen = None, {}
    for line in listing:
        if any(p in line for p in PROLOGUES):
            function = line.split("\t")[0]
        if ("\tbl\t" in line or "\tb\t" in line) and rx.search(line):
            seen.setdefault(function, []).append(line.split("\t")[0])
    for function, calls in seen.items():
        print(function, len(calls), " ".join(calls))


def main():
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("slices", "sections", "methods", "ivars", "annotate", "extract"):
        p = sub.add_parser(name)
        p.add_argument("binary")
        p.add_argument("--slice", type=int, default=0)
        if name == "ivars":
            p.add_argument("classes", nargs="*")
        if name == "extract":
            p.add_argument("directory")
    sub.add_parser("callers").add_argument("regex")
    args = parser.parse_args()

    if args.command == "callers":
        return cmd_callers(args.regex, sys.stdin)

    if args.command in ("slices", "extract"):
        data = Path(args.binary).read_bytes()
        base = Path(args.binary).name
        for i, (ct, cs, off, size) in enumerate(fat_slices(data)):
            arm64 = " arm64" if ct == CPU_TYPE_ARM64 else ""
            print(
                f"{i}: cputype {ct:#x}{arm64} cpusubtype {cs} offset {off:#x} size {size:#x}"
            )
            if args.command == "extract":
                Path(args.directory, f"{base}.{i}").write_bytes(data[off : off + size])
        return

    m = MachO(args.binary, args.slice)
    if args.command == "sections":
        for seg, sect, addr, size in m.sections:
            print(f"{seg},{sect} {addr:#x} {size:#x}")
    elif args.command == "methods":
        for cls, meta, sel, imp in m.methods():
            print(f"{imp:#x} {'+' if meta else '-'}[{cls} {sel}]")
    elif args.command == "ivars":
        for cls, name, type_, offset, size in m.ivars():
            if not args.classes or cls in args.classes:
                print(f"{cls} {name} {type_} +{offset:#x} size={size}")
    elif args.command == "annotate":
        stubs = m.objc_stubs()
        rx = re.compile(r"\bbl?\s+0x([0-9a-f]+)$")
        for line in sys.stdin:
            line = line.rstrip("\n")
            match = rx.search(line)
            if match and int(match.group(1), 16) in stubs:
                line += f" ; [{stubs[int(match.group(1), 16)]}]"
            print(line)


if __name__ == "__main__":
    main()
