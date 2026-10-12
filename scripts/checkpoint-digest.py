#!/usr/bin/env python3
"""Print a host-independent digest of a FESVR checkpoint.

The digest lists the header fields, every hart's decoded state and the SHA-256 of the decompressed memory image.
Two checkpoints of the same point of a run have the same digest, wherever they were created, even when their bytes
differ (the memory image is a zlib stream, and zlib builds may compress the same data differently).

The formats are defined in riscv-fesvr/fesvr/checkpoint_format.h and riscv-fesvr/fesvr/hart_state_format.h.

usage: checkpoint-digest.py <checkpoint> [<checkpoint> ...]
"""

import hashlib
import struct
import sys
import zlib

CHECKPOINT_MAGIC = 0x78642578
CHECKPOINT_FORMAT_VERSION = 1
HART_STATE_STORAGE_MAX = 8192
HART_STATE_MAGIC = b"RVHSTATE"
HART_STATE_VERSION = 1

CSR_NAMES = {
    0x001: "fflags", 0x002: "frm", 0x500: "sup0", 0x501: "sup1", 0x502: "epc", 0x503: "badvaddr",
    0x504: "ptbr", 0x506: "count", 0x507: "compare", 0x508: "evec", 0x509: "cause", 0x50a: "status",
    0x51e: "tohost", 0x51f: "fromhost",
}


class FormatError(Exception):
    pass


def entry_name(entry_id):
    if 0x1000 <= entry_id < 0x1020:
        return "x%d" % (entry_id - 0x1000)
    if 0x1100 <= entry_id < 0x1120:
        return "f%d" % (entry_id - 0x1100)
    if entry_id == 0x2000:
        return "pc"
    if entry_id == 0x2001:
        return "load_reservation"
    if entry_id < 0x1000:
        return CSR_NAMES.get(entry_id, "csr 0x%03x" % entry_id)
    return "id 0x%x" % entry_id


def read_exact(f, n, what):
    data = f.read(n)
    if len(data) != n:
        raise FormatError("truncated %s" % what)
    return data


def hart_state_lines(state):
    if len(state) < 16 or state[:8] != HART_STATE_MAGIC:
        raise FormatError('hart state is not in the portable format (no "RVHSTATE" magic)')
    version, count = struct.unpack_from("<II", state, 8)
    if version != HART_STATE_VERSION:
        raise FormatError("hart state format version %d, this script reads version %d" % (version, HART_STATE_VERSION))
    if len(state) != 16 + 16 * count:
        raise FormatError("hart state is %d bytes, which doesn't match its %d entries" % (len(state), count))
    entries = [struct.unpack_from("<IIQ", state, 16 + 16 * i) for i in range(count)]
    return ["  %-18s 0x%016x" % (entry_name(entry_id), value) for entry_id, _, value in sorted(entries)]


def digest(path):
    lines = []
    with open(path, "rb") as f:
        magic, version = struct.unpack("<II", read_exact(f, 8, "header"))
        if magic != CHECKPOINT_MAGIC:
            raise FormatError("bad magic number")
        if version != CHECKPOINT_FORMAT_VERSION:
            raise FormatError("checkpoint format version %d, this script reads version %d"
                              % (version, CHECKPOINT_FORMAT_VERSION))
        num_hart, mem_sz_mb = struct.unpack("<II", read_exact(f, 8, "header"))
        elf_sha256 = read_exact(f, 32, "header")
        lines.append("format version %d" % version)
        lines.append("harts %d" % num_hart)
        lines.append("memory %d MB" % mem_sz_mb)
        lines.append("boot ELF SHA256 %s" % elf_sha256.hex())
        for hart in range(num_hart):
            hart_data = read_exact(f, 32 + 8 + 4 + HART_STATE_STORAGE_MAX, "hart %d" % hart)
            recording_sha256 = hart_data[:32]
            skip_amt, state_size = struct.unpack_from("<QI", hart_data, 32)
            if state_size > HART_STATE_STORAGE_MAX:
                raise FormatError("hart %d state size %d exceeds its storage" % (hart, state_size))
            lines.append("hart %d recording SHA256 %s" % (hart, recording_sha256.hex()))
            lines.append("hart %d commands serviced %d" % (hart, skip_amt))
            lines.append("hart %d state:" % hart)
            lines.extend(hart_state_lines(hart_data[44:44 + state_size]))

        inflater = zlib.decompressobj()
        memory_sha256 = hashlib.sha256()
        memory_size = 0
        while True:
            chunk = f.read(1 << 20)
            if not chunk:
                break
            data = inflater.decompress(chunk)
            memory_sha256.update(data)
            memory_size += len(data)
            if inflater.eof:
                break
        if not inflater.eof:
            raise FormatError("truncated memory image")
        if inflater.unused_data or f.read(1):
            raise FormatError("unexpected data after the memory image")
        lines.append("memory image %d bytes, SHA256 %s" % (memory_size, memory_sha256.hexdigest()))
    return lines


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.strip())
    status = 0
    for path in sys.argv[1:]:
        if len(sys.argv) > 2:
            print("== %s" % path)
        try:
            print("\n".join(digest(path)))
        except (OSError, FormatError, zlib.error) as e:
            print("%s: %s" % (path, e), file=sys.stderr)
            status = 1
    sys.exit(status)


if __name__ == "__main__":
    main()
