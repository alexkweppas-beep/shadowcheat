import argparse
from pathlib import Path

import pefile

CUSTOMER_PLACEHOLDER = b"SHADOW_CUSTOMER_TAG_" + b"0" * 32
PLAINTEXT_MARKERS = (
    b"QUANT TOOLS v1.0",
    b"const __ffBuckets",
    b"g.__bfCwFn=function()",
    b"global.__uaLoaded",
    b"const rows=[],seen={};",
    b"__bfDmg1",
    b"__bfAutoLoot",
    b"VMProtectSDK64.dll",
)


def section_names(pe: pefile.PE) -> set[str]:
    return {
        section.Name.rstrip(b"\0").decode("ascii", errors="replace")
        for section in pe.sections
    }


def imported_libraries(pe: pefile.PE) -> set[str]:
    if not hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        return set()
    return {
        entry.dll.decode("ascii", errors="replace").lower()
        for entry in pe.DIRECTORY_ENTRY_IMPORT
    }


def audit_binary(
    path: Path,
    personalized: bool,
    variant_id: int | None,
) -> list[str]:
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=False)
    errors = []
    sections = section_names(pe)
    if not any(name.startswith(".svm") for name in sections):
        errors.append("VMProtect section is missing")
    if ".shid" not in sections:
        errors.append("customer identity section is missing")
    if "vmprotectsdk64.dll" in imported_libraries(pe):
        errors.append("VMProtect SDK import was not eliminated")
    for marker in PLAINTEXT_MARKERS:
        if marker in data:
            errors.append(f"plaintext marker remains: {marker!r}")
    placeholder_count = data.count(CUSTOMER_PLACEHOLDER)
    expected = 0 if personalized else 1
    if placeholder_count != expected:
        errors.append(
            f"customer placeholder count is {placeholder_count}, expected {expected}"
        )
    if variant_id is not None:
        marker = f"SHADOW_VARIANT_ID:{variant_id}".encode("ascii")
        if data.count(marker) != 1:
            errors.append(f"variant marker is missing or duplicated: {variant_id}")
    return errors


def audit_directory(path: Path) -> list[str]:
    errors = []
    forbidden = (
        "*.raw.dll",
        "*.pdb",
        "*.ilk",
        "*.build.vmp",
        "shadow_injector.exe",
    )
    for pattern in forbidden:
        for match in path.parent.glob(pattern):
            errors.append(f"forbidden shipping artifact: {match.name}")
    return errors


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--personalized", action="store_true")
    parser.add_argument("--shipping", action="store_true")
    parser.add_argument("--variant-id", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    errors = audit_binary(
        args.artifact,
        args.personalized,
        args.variant_id,
    )
    if args.shipping:
        errors.extend(audit_directory(args.artifact))
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    print(f"OK: {args.artifact.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
