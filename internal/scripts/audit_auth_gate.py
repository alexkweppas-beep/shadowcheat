import argparse
from pathlib import Path


def function_block(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    finish = source.index(next_signature, start)
    return source[start:finish]


def audit(root: Path) -> list[str]:
    errors = []
    cloud = (root / "src/Features/CloudStorage.cpp").read_text("utf-8")
    renew = function_block(
        cloud,
        "static VerificationResult RenewLaunchTicket()",
        "// ── Parse comma-separated")
    verify = function_block(
        cloud,
        "VerificationResult VerifySubscription()",
        "// ── List:")
    if "HttpMethod::Post" not in renew or 'L"/api/cheat/renew"' not in renew:
        errors.append("renew is not an explicit POST")
    if "/api/configs/list" in verify:
        errors.append("subscription verification still performs a second request")
    security = (root / "src/Security/shadow_verify.cpp").read_text("utf-8")
    if "std::chrono::minutes(1)" in security:
        errors.append("initial authorization still waits one minute")
    hooks = (root / "src/Main/Hooks.cpp").read_text("utf-8")
    if "if (!featuresReady)" not in hooks:
        errors.append("feature/menu gate is missing")
    if "WinHttp" in hooks:
        errors.append("render hook contains WinHTTP work")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, nargs="?", default=Path("."))
    errors = audit(parser.parse_args().root)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors))
        return 1
    print("OK: async authorization gate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
