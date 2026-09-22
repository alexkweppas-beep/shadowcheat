import argparse
import hashlib
from pathlib import Path

from audit_protected_artifact import audit_binary


def audit_catalog(root: Path, count: int) -> list[str]:
    errors = []
    hashes = set()
    for variant_id in range(count):
        artifact = root / f"{variant_id:02d}" / "cheat.dll"
        if not artifact.exists():
            errors.append(f"variant is missing: {variant_id:02d}")
            continue
        errors.extend(
            f"variant {variant_id:02d}: {error}"
            for error in audit_binary(artifact, False, variant_id)
        )
        hashes.add(hashlib.sha256(artifact.read_bytes()).hexdigest())
    if len(hashes) != count:
        errors.append(
            f"variant hashes are not unique: {len(hashes)} of {count}"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--count", type=int, default=32)
    args = parser.parse_args()
    if args.count < 2 or args.count > 64:
        print("ERROR: count must be between 2 and 64")
        return 1
    errors = audit_catalog(args.root, args.count)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors))
        return 1
    print(f"OK: {args.count} unique protected variants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
