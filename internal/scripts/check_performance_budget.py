import argparse
import json
from pathlib import Path

BUDGETS_US = {
    "Aimbot": 50.0,
    "FramePedContext": 5.0,
    "Esp": 5.0,
}


def read_stats(path: Path) -> dict[str, float]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    return {
        name: float(values["p99Us"])
        for name, values in raw.items()
    }


def compare(
    baseline: dict[str, float],
    candidate: dict[str, float],
) -> list[str]:
    errors = []
    for scope, budget in BUDGETS_US.items():
        if scope not in baseline or scope not in candidate:
            errors.append(f"missing scope: {scope}")
            continue
        increase = candidate[scope] - baseline[scope]
        if increase > budget:
            errors.append(
                f"{scope} p99 increased by {increase:.2f}us; budget {budget:.2f}us"
            )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    errors = compare(read_stats(args.baseline), read_stats(args.candidate))
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors))
        return 1
    print("OK: p99 performance budgets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
