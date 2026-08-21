# Extracts a vulnerable driver image from the KDU Tanikaze database (kdu.db).
#
# kdu.db layout (Source/Shared/kdubase.h):
#   RESOURCE_DB_HEADER { DWORD Signature='XDMR'; DWORD Version; DWORD EntryCount; }
#   RESOURCE_DB_ENTRY  { DWORD Id; DWORD Offset; DWORD Size; }[EntryCount]
#   ... blobs ...
#
# Each blob is XOR-"encrypted" with a rolling key (compress.cpp EncodeBuffer,
# PROVIDER_RES_KEY = 0xF62E6CE0) and then MSDelta RAW-compressed, so extraction
# is: decrypt -> ApplyDeltaB(DELTA_FILE_TYPE_RAW, empty, blob) -> PE image.

import ctypes
import hashlib
import struct
import sys
from ctypes import wintypes

PROVIDER_RES_KEY = 0xF62E6CE0
RESOURCE_DB_SIGNATURE = 0x58444D52  # 'XDMR' (C multichar constant)

# KDU provider id -> (resource id, expected file SHA1, expected image size)
# from Help/providers.md (KDU 1.50).
KNOWN = {
    20: (123, "90A76945FD2FA45FAB2B7BCFDAF6563595F94891", 24952),   # Dell DBUtil 2.5
    34: (137, "9745D77E3C27437BBCCF39E074F7D57A99FE83B1", 18688),   # MSI winio
    44: (146, "A24840E32071E0F64E1DFF8CA540604896811587", 30144),   # AMD PdFwKrnl
}


def rol32(v, n=1):
    return ((v << n) | (v >> (32 - n))) & 0xFFFFFFFF


def encode_buffer(buf, key):
    out = bytearray(buf)
    k = key
    for i in range(len(out)):
        out[i] ^= k & 0xFF
        k = rol32(k, 1)
    return bytes(out)


class DELTA_INPUT(ctypes.Structure):
    _fields_ = [
        ("lpcStart", wintypes.LPCVOID),
        ("uSize", ctypes.c_size_t),
        ("Editable", wintypes.BOOL),
    ]


class DELTA_OUTPUT(ctypes.Structure):
    _fields_ = [
        ("lpStart", wintypes.LPVOID),
        ("uSize", ctypes.c_size_t),
    ]


def apply_delta_raw(delta_bytes):
    msdelta = ctypes.WinDLL("msdelta.dll")
    ApplyDeltaB = msdelta.ApplyDeltaB
    ApplyDeltaB.argtypes = [ctypes.c_int, DELTA_INPUT, DELTA_INPUT,
                            ctypes.POINTER(DELTA_OUTPUT)]
    ApplyDeltaB.restype = wintypes.BOOL
    DeltaFree = msdelta.DeltaFree
    DeltaFree.argtypes = [wintypes.LPVOID]
    DeltaFree.restype = wintypes.BOOL

    delta_buf = ctypes.create_string_buffer(delta_bytes, len(delta_bytes))
    source = DELTA_INPUT(None, 0, False)
    delta = DELTA_INPUT(ctypes.cast(delta_buf, wintypes.LPCVOID),
                        len(delta_bytes), False)
    out = DELTA_OUTPUT(None, 0)
    if not ApplyDeltaB(0, source, delta, ctypes.byref(out)):
        raise RuntimeError("ApplyDeltaB failed")
    try:
        return ctypes.string_at(out.lpStart, out.uSize)
    finally:
        DeltaFree(out.lpStart)


def extract(db_path, provider_id):
    res_id, want_sha1, want_size = KNOWN[provider_id]
    db = open(db_path, "rb").read()
    sig, version, count = struct.unpack_from("<III", db, 0)
    if sig != RESOURCE_DB_SIGNATURE:
        raise RuntimeError("bad kdu.db signature 0x%08X" % sig)
    entries = {}
    off = 12
    for i in range(count):
        eid, eoff, esize = struct.unpack_from("<III", db, off + i * 12)
        entries[eid] = (eoff, esize)
    if res_id not in entries:
        raise RuntimeError("resource id %d not in db" % res_id)
    eoff, esize = entries[res_id]
    blob = db[eoff:eoff + esize]
    plain = apply_delta_raw(encode_buffer(blob, PROVIDER_RES_KEY))
    if plain[:2] != b"MZ":
        raise RuntimeError("decrypted blob is not a PE image")
    sha1 = hashlib.sha1(plain).hexdigest().upper()
    print("provider %d -> resource %d: %d bytes, sha1 %s" %
          (provider_id, res_id, len(plain), sha1))
    if sha1 != want_sha1:
        raise RuntimeError("SHA1 mismatch, expected " + want_sha1)
    if len(plain) != want_size:
        raise RuntimeError("size mismatch, expected %d" % want_size)
    return plain


def emit_c_array(data, symbol, out_path):
    lines = []
    lines.append("#pragma once")
    lines.append("// %s, %d bytes, extracted from the KDU 1.50 drivers database" %
                 (symbol, len(data)))
    lines.append("// (Tanikaze kdu.db). Signed driver, not on the Microsoft")
    lines.append("// vulnerable-driver blocklist at time of extraction.")
    lines.append("namespace %s {" % symbol)
    lines.append("    const unsigned char driver[] = {")
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("        " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("    };")
    lines.append("}")
    open(out_path, "w", newline="\n").write("\n".join(lines) + "\n")
    print("wrote " + out_path)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("usage: extract_kdu_driver.py <kdu.db> <provider_id> [out.h]")
        sys.exit(1)
    image = extract(sys.argv[1], int(sys.argv[2]))
    if len(sys.argv) > 3:
        emit_c_array(image, sys.argv[4] if len(sys.argv) > 4 else "vuln_driver_resource",
                     sys.argv[3])
    else:
        open("driver_%d.sys" % int(sys.argv[2]), "wb").write(image)
