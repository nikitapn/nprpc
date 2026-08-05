#!/usr/bin/env python3
"""
Unified build and test runner for NPRPC.

Stages (all enabled by default, skip with --skip-<stage>):
  cmake   - cmake --build (C++ + TS/JS via nprpc_js_test target)
  cpp     - run C++ tests via ctest
  js      - build + run Mocha tests in test/js
  swift   - gen stubs + Docker build/test (default), or host swift test

Usage:
  python3 run_all_tests.py [options]

  # Fast local loop: reuse the host CMake build for Swift (no Docker rebuild)
  python3 run_all_tests.py --swift-host

  # Pre-merge gate (all suites; Swift may be host or Docker)
  python3 run_all_tests.py
  python3 run_all_tests.py --swift-host

Options:
  --build-dir DIR        CMake build dir (default: .build_relwith_debinfo from .env)
  --skip-cmake           Skip CMake build step
  --skip-cpp             Skip C++ tests
  --skip-js              Skip JavaScript/TypeScript tests
  --skip-swift           Skip Swift tests
  --swift-host           Run Swift tests on the host against --build-dir
                         (skips Docker rebuild of nprpc; needs host Swift + setcap)
  --cmake-target TARGET  CMake build target (default: all)
  --cpp-filter FILTER    CTest regex filter for C++ tests (e.g. 'HTTP3Transport|HttpUtils')
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


def ensure_nprpc_bpf_capabilities(binary: Path) -> None:
    """Grant cap_net_admin,cap_bpf so HTTP/3 reuseport eBPF can attach."""
    if not binary.exists():
        return

    # Resolve symlinks (Swift SPM places xctest under both triple and debug/)
    binary = binary.resolve()
    if not binary.is_file():
        return

    getcap = shutil.which("getcap")
    setcap = shutil.which("setcap")
    if not getcap or not setcap:
        raise RuntimeError(
            "getcap/setcap is required to grant HTTP/3 reuseport BPF capabilities "
            f"(missing for {binary})"
        )

    current_caps = subprocess.run(
        [getcap, str(binary)],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    ).stdout

    if "cap_net_admin" in current_caps and "cap_bpf" in current_caps:
        return

    print(c(C.DIM, f"  setcap cap_net_admin,cap_bpf+ep → {binary}"))
    if os.geteuid() == 0:
        subprocess.run([setcap, "cap_net_admin,cap_bpf+ep", str(binary)], check=True)
        return

    subprocess.run(["sudo", setcap, "cap_net_admin,cap_bpf+ep", str(binary)], check=True)


def find_swift_test_binaries(swift_dir: Path) -> list[Path]:
    """Locate NPRPCPackageTests.xctest products under nprpc_swift/.build."""
    build_root = swift_dir / ".build"
    if not build_root.is_dir():
        return []
    found: dict[Path, Path] = {}
    for path in build_root.rglob("NPRPCPackageTests.xctest"):
        if path.is_file() or path.is_symlink():
            resolved = path.resolve()
            if resolved.is_file():
                found[resolved] = resolved
    return sorted(found.values())


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

    def stage_cpp(self, ctest_filter: Optional[str]) -> Result:
        ctest_dir = self.build_dir / "test"
        ctest_file = ctest_dir / "CTestTestfile.cmake"
        if not ctest_file.exists():
            r = Result(stage="C++ tests", success=False, duration=0,
                       output=f"CTest metadata not found: {ctest_file}\n")
            self.results.append(r)
            print(c(C.RED, f"  CTest metadata not found: {ctest_file}"))
            return r

        # Kill leftover helper processes from previous runs
        subprocess.run(["pkill", "-9", "npnameserver"], capture_output=True)
        subprocess.run(["pkill", "-9", "nprpc_server_test"], capture_output=True)
        ensure_nprpc_bpf_capabilities(self.build_dir / "test" / "nprpc_server_test")

        cmd = ["ctest", "--test-dir", str(ctest_dir), "--output-on-failure"]
        if ctest_filter:
            cmd.extend(["-R", ctest_filter])
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
        ensure_nprpc_bpf_capabilities(self.build_dir / "test" / "nprpc_server_test")

        cmd = ["npm", "run", "build"]
        r_build = self._stage("JS build (tsc)", cmd, cwd=js_dir, timeout=120)
        if not r_build.success:
            return r_build

        # Remove the intermediate build result; we report only the test result
        self.results.pop()

        return self._stage("JS tests (mocha)", ["npm", "test"], cwd=js_dir, timeout=120)

    def _gen_swift_stubs(self) -> Result | None:
        """Generate Swift stubs. Returns a failed Result on error, else None."""
        gen_ok, gen_out = self._run(["just", "gen-swift-stubs"], cwd=self.root, timeout=60)
        if gen_ok:
            return None
        r = Result(stage="Swift gen stubs", success=False, duration=0, output=gen_out)
        self.results.append(r)
        print(gen_out)
        return r

    def stage_swift(self, host: bool = False) -> Result:
        if host:
            return self.stage_swift_host()
        return self.stage_swift_docker()

    def stage_swift_docker(self) -> Result:
        """Full Docker path: rebuild nprpc with Swift's Clang, then swift test."""
        swift_dir = self.root / "nprpc_swift"

        failed = self._gen_swift_stubs()
        if failed is not None:
            return failed

        # Build Boost in Docker (only if not already present)
        boost_marker = self.root / ".build_ubuntu_swift" / "boost_install" / "include" / "boost"
        if not boost_marker.exists():
            print(c(C.DIM, "  Boost not found – running docker-build-boost.sh …"))
            ok, out = self._run(["bash", "docker-build-boost.sh"], cwd=swift_dir, timeout=900)
            if not ok:
                r = Result(stage="Swift build Boost", success=False, duration=0, output=out)
                self.results.append(r)
                print(out)
                return r

        r_build = self._stage(
            "Swift docker build",
            ["bash", "docker-build-nprpc.sh"],
            cwd=swift_dir,
            timeout=900,
        )
        if not r_build.success:
            return r_build

        return self._stage(
            "Swift tests (docker)",
            ["bash", "docker-build-swift.sh", "--test"],
            cwd=swift_dir,
            timeout=120,
        )

    def stage_swift_host(self) -> Result:
        """Host path: link against the existing CMake build_dir and run swift test.

        Avoids the long Docker rebuild of nprpc. The XCTest binary needs
        cap_net_admin+cap_bpf for HTTP/3 reuseport (sudo setcap).
        """
        swift_dir = self.root / "nprpc_swift"

        if not shutil.which("swift"):
            r = Result(
                stage="Swift tests (host)",
                success=False,
                duration=0,
                output="swift not found on PATH; install Swift or use Docker (omit --swift-host)\n",
            )
            self.results.append(r)
            print(r.output)
            return r

        lib = self.build_dir / "libnprpc.so"
        if not lib.exists():
            r = Result(
                stage="Swift tests (host)",
                success=False,
                duration=0,
                output=(
                    f"libnprpc.so not found at {lib}\n"
                    "Build the host CMake tree first (omit --skip-cmake) or pass --build-dir.\n"
                ),
            )
            self.results.append(r)
            print(r.output)
            return r

        failed = self._gen_swift_stubs()
        if failed is not None:
            return failed

        # Relative to monorepo root so Package.swift resolves NPRPC_BUILD_DIR
        # the same way as manual `cd nprpc_swift && NPRPC_BUILD_DIR=... swift test`.
        try:
            build_dir_env = str(self.build_dir.relative_to(self.root))
        except ValueError:
            build_dir_env = str(self.build_dir)

        env = {
            "NPRPC_ROOT": str(self.root),
            "NPRPC_BUILD_DIR": build_dir_env,
        }

        r_build = self._stage(
            "Swift build (host)",
            ["swift", "build", "--build-tests"],
            cwd=swift_dir,
            env=env,
            timeout=600,
        )
        if not r_build.success:
            return r_build
        # Collapse build into the final test result line
        self.results.pop()

        binaries = find_swift_test_binaries(swift_dir)
        if not binaries:
            r = Result(
                stage="Swift tests (host)",
                success=False,
                duration=0,
                output="NPRPCPackageTests.xctest not found under nprpc_swift/.build after swift build\n",
            )
            self.results.append(r)
            print(r.output)
            return r

        try:
            for binary in binaries:
                ensure_nprpc_bpf_capabilities(binary)
        except (RuntimeError, subprocess.CalledProcessError) as exc:
            r = Result(
                stage="Swift tests (host) setcap",
                success=False,
                duration=0,
                output=f"Failed to grant eBPF capabilities to Swift test binary: {exc}\n",
            )
            self.results.append(r)
            print(r.output)
            return r

        # --skip-build keeps setcap intact (a rebuild would strip file capabilities)
        return self._stage(
            "Swift tests (host)",
            ["swift", "test", "--skip-build"],
            cwd=swift_dir,
            env=env,
            timeout=180,
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
    p.add_argument(
        "--swift-host",
        action="store_true",
        help=(
            "Run Swift tests on the host against the CMake build dir "
            "(no Docker rebuild; requires host Swift and sudo setcap for eBPF)"
        ),
    )
    p.add_argument("--cmake-target", default="all",
                   help="CMake target to build (default: all)")
    p.add_argument("--cpp-filter",  default=None,
                   help="CTest regex filter for C++ tests (e.g. 'HTTP3Transport|HttpUtils')")
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
    if args.swift_host and not args.skip_swift:
        print(c(C.DIM,  f"swift     : host (NPRPC_BUILD_DIR={build_dir})"))
    elif not args.skip_swift:
        print(c(C.DIM,  "swift     : docker"))

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
        runner.stage_swift(host=args.swift_host)

    # --- Summary -----------------------------------------------------------
    ok = runner.print_summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
