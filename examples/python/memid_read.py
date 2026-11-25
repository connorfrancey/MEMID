"""Reading a binary's MEMID identity from Python, two ways:

    python memid_read.py --library path/to/memid.dll MyMod.dll   # through the C ABI (ctypes)
    python memid_read.py --direct MyMod.dll                      # the format itself: no MEMID library at all

The second way is the point of a documented binary format: any language can read it. It parses only PE files here
(the section table and the .memid section); see docs/MEMID_FORMAT.md for the layout.
"""
import argparse
import ctypes
import struct
import sys
import uuid

MAGIC = b"MEMID\r\n\x1a"


class DescriptorInfo(ctypes.Structure):
    """memid_descriptor_info from memid/c_api.h"""
    _fields_ = [
        ("struct_size", ctypes.c_uint32),
        ("format_major", ctypes.c_uint16), ("format_minor", ctypes.c_uint16),
        ("flags", ctypes.c_uint32),
        ("mod_id", ctypes.c_uint8 * 16), ("publisher_id", ctypes.c_uint8 * 16), ("build_id", ctypes.c_uint8 * 16),
        ("version_major", ctypes.c_uint16), ("version_minor", ctypes.c_uint16), ("version_patch", ctypes.c_uint16),
        ("release_channel", ctypes.c_uint16), ("architecture", ctypes.c_uint16), ("operating_system", ctypes.c_uint16),
        ("binary_format", ctypes.c_uint8), ("language", ctypes.c_uint8),
        ("runtime", ctypes.c_uint16),
        ("build_timestamp", ctypes.c_uint64), ("toolchain_id", ctypes.c_uint64),
        ("game_id", ctypes.c_uint32), ("game_version_min", ctypes.c_uint32), ("game_version_max", ctypes.c_uint32),
        ("interop_min", ctypes.c_uint32), ("interop_max", ctypes.c_uint32),
        ("capabilities", ctypes.c_uint64), ("protocol_flags", ctypes.c_uint64),
        ("extension_count", ctypes.c_uint32),
        ("reverse_domain", ctypes.c_char * 97), ("name", ctypes.c_char * 65),
        ("publisher", ctypes.c_char * 49), ("homepage", ctypes.c_char * 65),
    ]


def via_c_abi(library, path):
    memid = ctypes.CDLL(library)
    memid.memid_read_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(DescriptorInfo)]
    memid.memid_read_file.restype = ctypes.c_int
    memid.memid_status_text.argtypes = [ctypes.c_int]
    memid.memid_status_text.restype = ctypes.c_char_p
    info = DescriptorInfo()
    info.struct_size = ctypes.sizeof(DescriptorInfo)
    status = memid.memid_read_file(path.encode("utf-8"), ctypes.byref(info))
    if status not in (0, 12):  # MEMID_OK, MEMID_ERROR_INVALID
        return None, memid.memid_status_text(status).decode()
    return {
        "name": info.name.decode("utf-8"),
        "reverse_domain": info.reverse_domain.decode("utf-8"),
        "version": f"{info.version_major}.{info.version_minor}.{info.version_patch}",
        "mod_id": str(uuid.UUID(bytes=bytes(info.mod_id))),
    }, None


def pe_section(data, wanted):
    """(offset, size) of a PE section, or None"""
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    nt = struct.unpack_from("<I", data, 0x3C)[0]
    if data[nt:nt + 4] != b"PE\0\0":
        raise ValueError("no NT headers")
    count, = struct.unpack_from("<H", data, nt + 6)
    optional, = struct.unpack_from("<H", data, nt + 20)
    table = nt + 24 + optional
    for i in range(count):
        name, vsize, _, rsize, roff = struct.unpack_from("<8sIIII", data, table + i * 40)
        if name.rstrip(b"\0") == wanted:
            return roff, min(vsize, rsize) if vsize else rsize
    return None


def text(field):
    return field.split(b"\0", 1)[0].decode("utf-8")


def direct(path):
    data = open(path, "rb").read()
    section = pe_section(data, b".memid")
    if section is None:
        return None, "no .memid section"
    offset, size = section
    body = data[offset:offset + size]
    at = body.find(MAGIC)
    if at < 0:
        return None, "no descriptor in .memid"
    d = body[at:]
    if len(d) < 428:
        return None, "truncated"
    major, minor, length = struct.unpack_from("<HHI", d, 8)
    if major != 1 or length < 428:
        return None, f"unsupported format {major}.{minor}"
    vmaj, vmin, vpat = struct.unpack_from("<HHH", d, 52)
    return {
        "name": text(d[240:304]),
        "reverse_domain": text(d[144:240]),
        "version": f"{vmaj}.{vmin}.{vpat}",
        "mod_id": str(uuid.UUID(bytes=d[20:36])),
    }, None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--library", help="path to the MEMID shared library (memid.dll / libmemid.so)")
    parser.add_argument("--direct", action="store_true", help="parse the binary format in Python, without the library")
    parser.add_argument("file")
    args = parser.parse_args()
    if not args.direct and not args.library:
        parser.error("give --library PATH or --direct")
    identity, error = direct(args.file) if args.direct else via_c_abi(args.library, args.file)
    if error:
        print(f"{args.file}: no identity ({error})")
        return 3
    print(f"{identity['name']} {identity['version']} ({identity['reverse_domain']}) {identity['mod_id']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
