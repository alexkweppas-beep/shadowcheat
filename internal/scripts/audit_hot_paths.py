import argparse
from pathlib import Path

HOT_FILES = (
    "src/Main/Hooks.cpp",
    "src/Features/Aimbot.cpp",
    "src/Features/Visuals.cpp",
    "src/Features/Visibility.cpp",
)


def audit(root: Path) -> list[str]:
    errors = []
    for relative in HOT_FILES:
        source = (root / relative).read_text(encoding="utf-8")
        if "SHADOW_PROTECT_" in source:
            errors.append(f"VMProtect marker entered hot path: {relative}")
    silent = (root / "src/Features/SilentAim.cpp").read_text(encoding="utf-8")
    markers = [
        line.strip()
        for line in silent.splitlines()
        if any(token in line for token in (
            "SHADOW_PROTECT_ULTRA(",
            "SHADOW_PROTECT_VIRT(",
            "SHADOW_PROTECT_MUT(",
        ))
    ]
    expected = ['SHADOW_PROTECT_ULTRA("Silent.Initialize");']
    if markers != expected:
        errors.append(f"SilentAim markers changed: {markers}")
    combat = (root / "src/Features/CombatBake.cpp").read_text("utf-8")
    if "kPackedMilli" in combat or "kPackedRaw" in combat:
        errors.append("combat policy is still embedded in the DLL source")
    bundle_users = {
        "FiveRpFarmBot.cpp": "FiveRpFarmBotScript",
        "QuantTools.cpp": "QuantToolsScript",
        "UkraineTools.cpp": "UkraineToolsScript",
    }
    for file_name, embedded_name in bundle_users.items():
        source = (root / "src/Features" / file_name).read_text("utf-8")
        if embedded_name in source or "FeatureBundles::" not in source:
            errors.append(f"on-demand bundle gate is missing: {file_name}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, nargs="?", default=Path("."))
    args = parser.parse_args()
    errors = audit(args.root)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors))
        return 1
    print("OK: hot paths contain no VMProtect entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
