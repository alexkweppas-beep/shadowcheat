import argparse
import hashlib
import os
import re
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM

RAW_LITERAL = re.compile(
    r'R"(?P<delimiter>[A-Za-z0-9_]{0,16})\((?P<body>.*?)\)(?P=delimiter)"',
    re.DOTALL,
)


def extract_prefixed_literals(path: Path, prefix: str) -> bytes:
    source = path.read_text(encoding="utf-8")
    parts = [
        match.group("body")
        for match in RAW_LITERAL.finditer(source)
        if match.group("delimiter").startswith(prefix)
    ]
    if not parts:
        raise ValueError(f"No {prefix} raw strings found in {path}")
    return "".join(parts).encode("utf-8")


def extract_click_warp(path: Path) -> bytes:
    source = path.read_text(encoding="utf-8")
    start = source.index("bool EnsureClickWarpHook()")
    finish = source.index("// Defined further down", start)
    block = source[start:finish]
    match = next(
        (
            item
            for item in RAW_LITERAL.finditer(block)
            if item.group("delimiter") == "JS"
        ),
        None,
    )
    if not match:
        raise ValueError("Click-warp script was not found")
    return match.group("body").encode("utf-8")


def byte_array(name: str, data: bytes) -> str:
    values = ",".join(f"0x{value:02x}" for value in data)
    return f"inline constexpr std::uint8_t {name}[] = {{{values}}};"


def encrypt_script(key: bytes, source: bytes) -> tuple[bytes, bytes, bytes]:
    nonce = os.urandom(12)
    encrypted = AESGCM(key).encrypt(nonce, source, None)
    return nonce, encrypted[-16:], encrypted[:-16]


def blob_lines(
    name: str,
    key_parts: tuple[bytes, bytes],
    source: bytes,
) -> list[str]:
    masked_key, key_mask = key_parts
    key = bytes(left ^ right for left, right in zip(masked_key, key_mask))
    nonce, tag, ciphertext = encrypt_script(key, source)
    return [
        byte_array(f"k{name}Nonce", nonce),
        byte_array(f"k{name}Tag", tag),
        byte_array(f"k{name}Ciphertext", ciphertext),
        f"inline constexpr SecureScriptBlob::Blob k{name}{{"
        f"k{name}Ciphertext,sizeof(k{name}Ciphertext),k{name}Nonce,k{name}Tag,"
        "kMaskedKey,kKeyMask};",
    ]

def export_feature_sources(args: argparse.Namespace) -> None:
    if not args.bundle_dir:
        return
    output = Path(args.bundle_dir)
    output.mkdir(parents=True, exist_ok=True)
    sources = {
        "five_rp.js": extract_prefixed_literals(Path(args.farm), "FIVEFARM"),
        "quant.js": extract_prefixed_literals(Path(args.quant), "QTJSX91"),
        "ukraine.js": extract_prefixed_literals(Path(args.ukraine), "UAJS"),
    }
    for name, source in sources.items():
        (output / name).write_bytes(source)


def generate(args: argparse.Namespace) -> None:
    if args.output:
        seed = bytes.fromhex(args.variant_seed)
        key = hashlib.sha256(b"script-key:" + seed).digest()
        mask = hashlib.sha256(b"script-mask:" + seed).digest()
        masked = bytes(left ^ right for left, right in zip(key, mask))
        click_warp = extract_click_warp(Path(args.misc))
        lines = [
            "#pragma once",
            "#include <cstdint>",
            '#include "Security/secure_script_blob.hpp"',
            "namespace ProtectedScriptBlobs {",
            byte_array("kMaskedKey", masked),
            byte_array("kKeyMask", mask),
        ]
        lines.extend(blob_lines("ClickWarp", (masked, mask), click_warp))
        lines.append("}")
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    export_feature_sources(args)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--farm", required=True)
    parser.add_argument("--quant", required=True)
    parser.add_argument("--misc", required=True)
    parser.add_argument("--ukraine", required=True)
    parser.add_argument("--output")
    parser.add_argument("--bundle-dir")
    parser.add_argument(
        "--variant-seed",
        default="0000000000000001",
    )
    return parser.parse_args()


if __name__ == "__main__":
    generate(parse_args())
