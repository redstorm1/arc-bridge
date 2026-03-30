from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def find_compiler() -> str:
    candidates = []
    if os.environ.get("CXX"):
        candidates.append(os.environ["CXX"])
    candidates.append(
        str(Path.home() / ".platformio" / "packages" / "toolchain-gccmingw32" / "bin" / "g++.exe")
    )
    candidates.extend(["c++", "g++", "clang++"])

    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        if Path(candidate).exists():
            return candidate
    raise SystemExit("No C++ compiler found in PATH")


def find_std_flag(compiler: str, repo_root: Path) -> str:
    candidates = ["-std=c++17", "-std=gnu++17", "-std=c++1z", "-std=gnu++1z"]
    with tempfile.TemporaryDirectory() as tmpdir:
        source = Path(tmpdir) / "probe.cpp"
        binary = Path(tmpdir) / ("probe.exe" if os.name == "nt" else "probe")
        source.write_text("int main() { return 0; }\n", encoding="utf-8")
        for flag in candidates:
            result = subprocess.run(
                [compiler, flag, str(source), "-o", str(binary)],
                cwd=repo_root,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if result.returncode == 0:
                return flag
    raise SystemExit("No supported C++17-compatible standard flag found for the detected compiler")


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    component_dir = repo_root / "esphome" / "components" / "arc_bridge"
    test_cpp = repo_root / "tests" / "delivery_test.cpp"
    delivery_cpp = component_dir / "delivery.cpp"

    compiler = find_compiler()
    std_flag = find_std_flag(compiler, repo_root)
    with tempfile.TemporaryDirectory() as tmpdir:
        binary = Path(tmpdir) / ("delivery_test.exe" if os.name == "nt" else "delivery_test")
        cmd = [
            compiler,
            std_flag,
            "-Wall",
            "-Wextra",
            "-pedantic",
            str(test_cpp),
            str(delivery_cpp),
            "-I",
            str(component_dir),
            "-o",
            str(binary),
        ]
        subprocess.run(cmd, check=True, cwd=repo_root)
        subprocess.run([str(binary)], check=True, cwd=repo_root)


if __name__ == "__main__":
    main()
