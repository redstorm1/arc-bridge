from __future__ import annotations

from pathlib import Path


def require(text: str, needle: str, path: Path) -> None:
    if needle not in text:
        raise SystemExit(f"Missing expected text in {path}: {needle}")


def forbid(text: str, needle: str, path: Path) -> None:
    if needle in text:
        raise SystemExit(f"Found stale text in {path}: {needle}")


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    docs = [
        repo_root / "README.md",
        repo_root / "esphome" / "components" / "arc_bridge" / "README.md",
    ]

    required_transport = "not a raw Pulse 1 RS485 implementation"
    stale_services = [
        "arc_start_discovery",
        "arc_stop_discovery",
        "arc_query_all",
        "arc_pair",
    ]

    for path in docs:
        text = path.read_text(encoding="utf-8")
        require(text, required_transport, path)
        require(text, "arc_bridge_group", path)
        require(text, "battery_level", path)
        require(text, "send_query_all()", path)
        require(text, "send_favorite(", path)
        require(text, "send_jog_open(", path)
        require(text, "send_jog_close(", path)
        for needle in stale_services:
            forbid(text, needle, path)


if __name__ == "__main__":
    main()
