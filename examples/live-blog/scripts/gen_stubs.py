#!/usr/bin/env python3
"""
Generate NPRPC stubs for TypeScript and Swift from IDL files.
Uses the nprpc-dev:latest Docker image which contains the npidl compiler.

Usage:
    python3 gen_stubs.py          # generate both TS and Swift
    python3 gen_stubs.py --ts     # generate TypeScript only
    python3 gen_stubs.py --swift  # generate Swift only
"""

import subprocess
import sys
import os
import argparse
from pathlib import Path

ROOT_DIR = Path(__file__).parent.parent.resolve()
# Change working directory to `live-blog`
os.chdir(ROOT_DIR)

DOCKER_IMAGE = "nprpc-dev:latest"
REPO_ROOT = ROOT_DIR.parent.parent.resolve()
LOCAL_NPIDL_CANDIDATES = [
    REPO_ROOT / ".build_relwith_debinfo" / "npidl" / "npidl",
    REPO_ROOT / ".build_release" / "npidl" / "npidl",
    REPO_ROOT / ".build" / "npidl" / "npidl",
]

# Input IDL files
IDL_FILES = [
    "idl/live_blog.npidl",
]

# Output directories (relative to ROOT_DIR, mirrored as /app/... inside container)
TS_OUTPUT_DIR    = "client/src/rpc"
SWIFT_OUTPUT_DIR = "swift/Sources/LiveBlogAPI"

def run_npidl(lang_flag: str, idl_files: list, output_dir: str) -> None:
    """Run npidl in the nprpc-dev Docker container for the given language."""
    local_npidl = next((candidate for candidate in LOCAL_NPIDL_CANDIDATES if candidate.exists()), None)
    if local_npidl is not None:
        output_path = ROOT_DIR / output_dir
        output_path.mkdir(parents=True, exist_ok=True)
        cmd = [str(local_npidl), lang_flag, "--output-dir", str(output_path), *idl_files]
        print(f"  npidl {lang_flag}  ->  {output_dir} (local: {local_npidl.relative_to(REPO_ROOT)})")
        try:
            subprocess.run(cmd, check=True, cwd=ROOT_DIR)
            return
        except subprocess.CalledProcessError as e:
            print(f"WARN: local npidl failed (exit {e.returncode}), falling back to Docker", file=sys.stderr)

    container_output = f"/app/{output_dir}"
    container_idl_files = " ".join(f"/app/{f}" for f in idl_files)

    # Use sh -c so we can do mkdir -p first in a single docker run
    inner_cmd = (
        f"mkdir -p {container_output} && "
        f"npidl {lang_flag} --output-dir {container_output} {container_idl_files}"
    )

    cmd = [
        "docker", "run", "--rm",
        "--user", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{ROOT_DIR}:/app",
        "-w", "/app",
        DOCKER_IMAGE,
        "sh", "-c", inner_cmd,
    ]

    print(f"  npidl {lang_flag}  ->  {output_dir} (docker)")
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        print(f"ERROR: npidl failed (exit {e.returncode})", file=sys.stderr)
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate NPRPC stubs via Docker npidl")
    parser.add_argument("--ts",    action="store_true", help="Generate TypeScript stubs only")
    parser.add_argument("--swift", action="store_true", help="Generate Swift stubs only")
    args = parser.parse_args()

    # Default: generate both
    gen_ts    = args.ts    or (not args.ts and not args.swift)
    gen_swift = args.swift or (not args.ts and not args.swift)

    print("=== Generating NPRPC stubs ===")
    print(f"  Docker image : {DOCKER_IMAGE}")
    print(f"  IDL files  : {', '.join(IDL_FILES)}")

    print()

    if gen_ts:
        run_npidl("--ts", IDL_FILES, TS_OUTPUT_DIR)

    if gen_swift:
        run_npidl("--swift", IDL_FILES, SWIFT_OUTPUT_DIR)

    print()
    print("Done.")


if __name__ == "__main__":
    main()
