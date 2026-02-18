#!/usr/bin/env python3
"""
Unified build and test runner for NPRPC.

Stages (all enabled by default, skip with --skip-<stage>):
  cmake   - cmake --build (C++ + TS/JS via nprpc_js_test target)
  cpp     - run nprpc_test binary via ctest
  js      - build + run Mocha tests in test/js
  swift   - gen stubs + docker-build + docker test

Usage:
  python3 run_all_tests.py [options]

Options:
  --build-dir DIR        CMake build dir (default: .build_relwith_debinfo from .env)
  --skip-cmake           Skip CMake build step
  --skip-cpp             Skip C++ tests
  --skip-js              Skip JavaScript/TypeScript tests
  --skip-swift           Skip Swift tests
  --cmake-target TARGET  CMake build target (default: all)
  --cpp-filter FILTER    gtest filter passed to nprpc_test (e.g. '*Basic*')
  --color                Force coloured output even when not a TTY
  -v, --verbose          Show full output from sub-commands (default: only on failure)
  -h, --help             Show this help message
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------------------
# Colours
# ---------------------------------------------------------------------------

class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    RED    = "\033[31m"
    GREEN  = "\033[32m"
    YELLOW = "\033[33m"
    CYAN   = "\033[36m"
    DIM    = "\033[2m"

def _use_color(force: bool) -> bool:
    return force or (sys.stdout.isatty() and os.environ.get("NO_COLOR") is None)

USE_COLOR = False  # set after arg parse

def c(color: str, text: str) -> str:
    return f"{color}{text}{C.RESET}" if USE_COLOR else text

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _env_build_dir() -> str:
    """Read BUILD_DIR from .env if it exists."""
    env_file = Path(__file__).parent / ".env"
    if env_file.exists():
        for line in env_file.read_text().splitlines():
            m = re.match(r"^\s*BUILD_DIR\s*=\s*(.+)", line)
            if m:
                return m.group(1).strip()
    return ".build_relwith_debinfo"


@dataclass
class Result:
    stage: str
    success: bool
    duration: float
    output: str = ""
    skip: bool = False


@dataclass
class Runner:
    root: Path
    build_dir: Path
    verbose: bool
    results: list[Result] = field(default_factory=list)

    # ------------------------------------------------------------------
    def _run(self,
             cmd: list[str],
             *,
             cwd: Optional[Path] = None,
             env: Optional[dict] = None,
             timeout: int = 300) -> tuple[bool, str]:
        """Run *cmd*, return (success, combined_output)."""
        cwd = cwd or self.root
        merged_env = {**os.environ, **(env or {})}
        t0 = time.monotonic()
        try:
            proc = subprocess.run(
                cmd,
                cwd=str(cwd),
                env=merged_env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=timeout,
            )
            ok = proc.returncode == 0
            output = proc.stdout.decode(errors="replace")
            return ok, output
        except subprocess.TimeoutExpired:
            return False, f"TIMEOUT after {timeout}s\n"
        except FileNotFoundError as exc:
            return False, f"Command not found: {exc}\n"

    def _stage(self,
               name: str,
               cmd: list[str],
               *,
               cwd: Optional[Path] = None,
               env: Optional[dict] = None,
               timeout: int = 300) -> Result:
        print(c(C.CYAN + C.BOLD, f"\n{'━'*60}"))
        print(c(C.CYAN + C.BOLD, f"  STAGE: {name}"))
        print(c(C.CYAN + C.BOLD, f"{'━'*60}"))
        print(c(C.DIM, f"  cmd : {' '.join(cmd)}"))
        print(c(C.DIM, f"  cwd : {cwd or self.root}\n"))
        t0 = time.monotonic()
        ok, output = self._run(cmd, cwd=cwd, env=env, timeout=timeout)
        dt = time.monotonic() - t0
        status_str = c(C.GREEN, "PASSED") if ok else c(C.RED, "FAILED")
        print(c(C.DIM, output) if (ok and self.verbose) else ("" if ok else output))
        print(f"  → {status_str}  ({dt:.1f}s)")
        r = Result(stage=name, success=ok, duration=dt, output=output)
        self.results.append(r)
        return r

    # ------------------------------------------------------------------
    # Stage implementations
    # ------------------------------------------------------------------

    def stage_cmake(self, target: str) -> Result:
        nproc = str(os.cpu_count() or 4)
        cmd = ["cmake", "--build", str(self.build_dir), "--target", target, f"-j{nproc}"]
        return self._stage("CMake build", cmd, timeout=600)

    def stage_cpp(self, gtest_filter: Optional[str]) -> Result:
        test_bin = self.build_dir / "test" / "nprpc_test"
        if not test_bin.exists():
            r = Result(stage="C++ tests", success=False, duration=0,
                       output=f"Test binary not found: {test_bin}\n")
            self.results.append(r)
            print(c(C.RED, f"  Test binary not found: {test_bin}"))
            return r

        # Kill leftover nameserver from previous run
        subprocess.run(["pkill", "-9", "npnameserver"], capture_output=True)

        cmd = [str(test_bin)]
        if gtest_filter:
            cmd.append(f"--gtest_filter={gtest_filter}")
        return self._stage("C++ tests", cmd, timeout=120)

    def stage_js(self) -> Result:
        js_dir = self.root / "test" / "js"

        # Ensure dependencies are installed
        if not (js_dir / "node_modules").exists():
            print(c(C.DIM, "  Installing npm dependencies…"))
            ok, out = self._run(["npm", "ci"], cwd=js_dir, timeout=120)
            if not ok:
                r = Result(stage="JS tests (npm ci)", success=False, duration=0, output=out)
                self.results.append(r)
                print(out)
                return r

        # Kill leftover processes
        subprocess.run(["killall", "-9", "nprpc_server_test", "npnameserver"],
                       capture_output=True)

        cmd = ["npm", "run", "build"]
        r_build = self._stage("JS build (tsc)", cmd, cwd=js_dir, timeout=120)
        if not r_build.success:
            return r_build

        # Remove the intermediate build result; we report only the test result
        self.results.pop()

        return self._stage("JS tests (mocha)", ["npm", "test"], cwd=js_dir, timeout=120)

    def stage_swift(self) -> Result:
        swift_dir = self.root / "nprpc_swift"

        # 1. Generate stubs (requires npidl already built by CMake)
        gen_ok, gen_out = self._run(["bash", "gen_stubs.sh"], cwd=swift_dir, timeout=60)
        if not gen_ok:
            r = Result(stage="Swift gen stubs", success=False, duration=0, output=gen_out)
            self.results.append(r)
            print(gen_out)
            return r

        # 2. Build Boost in Docker (only if not already present)
        boost_marker = self.root / ".build_ubuntu_swift" / "boost_install" / "include" / "boost"
        if not boost_marker.exists():
            print(c(C.DIM, "  Boost not found – running docker-build-boost.sh …"))
            ok, out = self._run(["bash", "docker-build-boost.sh"], cwd=swift_dir, timeout=900)
            if not ok:
                r = Result(stage="Swift build Boost", success=False, duration=0, output=out)
                self.results.append(r)
                print(out)
                return r

        # 3. Build nprpc + Swift package in Docker
        r_build = self._stage(
            "Swift docker build",
            ["bash", "docker-build-nprpc.sh"],
            cwd=swift_dir,
            timeout=900,
        )
        if not r_build.success:
            return r_build

        # 4. Run Swift tests in Docker
        return self._stage(
            "Swift tests",
            ["bash", "docker-build-swift.sh", "--test"],
            cwd=swift_dir,
            timeout=120,
        )

    # ------------------------------------------------------------------
    def print_summary(self) -> bool:
        print(c(C.CYAN + C.BOLD, f"\n{'━'*60}"))
        print(c(C.CYAN + C.BOLD,  "  SUMMARY"))
        print(c(C.CYAN + C.BOLD, f"{'━'*60}"))
        all_ok = True
        for r in self.results:
            if r.skip:
                icon = c(C.YELLOW, "  SKIP ")
            elif r.success:
                icon = c(C.GREEN,  "  PASS ")
            else:
                icon = c(C.RED,    "  FAIL ")
                all_ok = False
            print(f"{icon}  {r.stage:<35}  {r.duration:5.1f}s")
        print()
        if all_ok:
            print(c(C.GREEN + C.BOLD, "  All stages passed ✓"))
        else:
            print(c(C.RED + C.BOLD, "  Some stages FAILED ✗"))
        print()
        return all_ok


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Unified build + test runner for NPRPC",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--build-dir", default=None,
                   help="CMake build directory (default: from .env or .build_relwith_debinfo)")
    p.add_argument("--skip-cmake",  action="store_true", help="Skip CMake build")
    p.add_argument("--skip-cpp",    action="store_true", help="Skip C++ tests")
    p.add_argument("--skip-js",     action="store_true", help="Skip JS/TS tests")
    p.add_argument("--skip-swift",  action="store_true", help="Skip Swift tests")
    p.add_argument("--cmake-target", default="all",
                   help="CMake target to build (default: all)")
    p.add_argument("--cpp-filter",  default=None,
                   help="GTest filter for C++ tests (e.g. '*Basic*')")
    p.add_argument("--color",       action="store_true", help="Force coloured output")
    p.add_argument("-v", "--verbose", action="store_true",
                   help="Show full subprocess output even on success")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    global USE_COLOR
    USE_COLOR = _use_color(args.color)

    root = Path(__file__).parent.resolve()
    build_dir_str = args.build_dir or _env_build_dir()
    build_dir = root / build_dir_str if not Path(build_dir_str).is_absolute() else Path(build_dir_str)

    print(c(C.BOLD, f"\nNPRPC unified build + test runner"))
    print(c(C.DIM,  f"root      : {root}"))
    print(c(C.DIM,  f"build_dir : {build_dir}"))

    runner = Runner(root=root, build_dir=build_dir, verbose=args.verbose)

    # --- CMake build -------------------------------------------------------
    if args.skip_cmake:
        runner.results.append(Result("CMake build", True, 0, skip=True))
    else:
        r = runner.stage_cmake(args.cmake_target)
        if not r.success:
            runner.print_summary()
            return 1

    # --- C++ tests ---------------------------------------------------------
    if args.skip_cpp:
        runner.results.append(Result("C++ tests", True, 0, skip=True))
    else:
        runner.stage_cpp(args.cpp_filter)

    # --- JS/TS tests -------------------------------------------------------
    if args.skip_js:
        runner.results.append(Result("JS tests (mocha)", True, 0, skip=True))
    else:
        runner.stage_js()

    # --- Swift tests -------------------------------------------------------
    if args.skip_swift:
        runner.results.append(Result("Swift tests", True, 0, skip=True))
    else:
        runner.stage_swift()

    # --- Summary -----------------------------------------------------------
    ok = runner.print_summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
