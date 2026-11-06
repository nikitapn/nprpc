#!/usr/bin/env python3
"""
Generate NPRPC stubs for TypeScript and Swift from IDL files.
Uses the npidl given by --npidl (or the NPIDL environment variable), else the
one in the nprpc-dev:latest Docker image.

Usage:
    python3 gen_stubs.py                      # generate TS and Swift
    python3 gen_stubs.py --ts                 # generate TypeScript only
    python3 gen_stubs.py --swift              # generate Swift only
    python3 gen_stubs.py --npidl path/npidl   # use a local npidl
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

# Input IDL files
IDL_FILES = [
    "idl/live_blog.npidl",
]

# Output directories (relative to ROOT_DIR, mirrored as /app/... inside container)
TS_OUTPUT_DIR    = "client/src/rpc"
SWIFT_OUTPUT_DIR = "swift/Sources/LiveBlogAPI"

def run_npidl(lang_flag: str, idl_files: list, output_dir: str, local_npidl: Path | None) -> None:
    """Run npidl for the given language: the local binary if given, else Docker."""
    if local_npidl is not None:
        output_path = ROOT_DIR / output_dir
        output_path.mkdir(parents=True, exist_ok=True)
        cmd = [str(local_npidl), lang_flag, "--output-dir", str(output_path), *idl_files]
        print(f"  npidl {lang_flag}  ->  {output_dir} (local: {local_npidl})")
        try:
            subprocess.run(cmd, check=True, cwd=ROOT_DIR)
        except subprocess.CalledProcessError as e:
            print(f"ERROR: npidl failed (exit {e.returncode})", file=sys.stderr)
            sys.exit(1)
        return

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
    parser = argparse.ArgumentParser(description="Generate NPRPC stubs for the live-blog example")
    parser.add_argument("--ts",    action="store_true", help="Generate TypeScript stubs only")
    parser.add_argument("--swift", action="store_true", help="Generate Swift stubs only")
    parser.add_argument("--npidl", type=Path, default=os.environ.get("NPIDL") or None,
                        help="Local npidl binary to use instead of the Docker image (default: $NPIDL)")
    args = parser.parse_args()

    local_npidl = args.npidl.resolve() if args.npidl else None
    if local_npidl is not None and not local_npidl.is_file():
        print(f"ERROR: npidl not found: {local_npidl}", file=sys.stderr)
        sys.exit(1)

    # No language flag at all means every language; naming one means only that
    # one (so --ts must not also drag in Swift).
    none_selected = not (args.ts or args.swift)
    gen_ts    = args.ts    or none_selected
    gen_swift = args.swift or none_selected

    print("=== Generating NPRPC stubs ===")
    print(f"  npidl      : {local_npidl or DOCKER_IMAGE + ' (docker)'}")
    print(f"  IDL files  : {', '.join(IDL_FILES)}")

    print()

    if gen_ts:
        run_npidl("--ts", IDL_FILES, TS_OUTPUT_DIR, local_npidl)

    if gen_swift:
        run_npidl("--swift", IDL_FILES, SWIFT_OUTPUT_DIR, local_npidl)

    print()
    print("Done.")


if __name__ == "__main__":
    main()
