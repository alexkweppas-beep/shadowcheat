import argparse
from pathlib import Path

import pefile


def audit(path: Path) -> list[str]:
    pe = pefile.PE(str(path), fast_load=False)
    sections = {
        section.Name.rstrip(b"\0").decode("ascii", errors="replace")
        for section in pe.sections
    }
    errors = []
    if not any(name.startswith(".svm") for name in sections):
        errors.append("VMProtect section is missing")
    imports = {
        entry.dll.decode("ascii", errors="replace").lower()
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", ())
    }
    if "vmprotectsdk64.dll" in imports:
        errors.append("VMProtect SDK import was not eliminated")
    for pattern in ("*.raw.exe", "*.pdb", "*.ilk", "*.build.vmp"):
        errors.extend(
            f"forbidden shipping artifact: {item.name}"
            for item in path.parent.glob(pattern)
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact", type=Path)
    args = parser.parse_args()
    errors = audit(args.artifact)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors))
        return 1
    print(f"OK: {args.artifact.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
