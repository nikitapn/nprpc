#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export NPRPC_ROOT="$PWD"
export NPRPC_SWIFT_PATH="$PWD/nprpc_swift"
export NPRPC_BUILD_DIR="${NPRPC_BUILD_DIR:-.build_relwith_debinfo}"
# Use the same library for SHM control-plane calls and remote QUIC calls.
# Put it ahead of desktop-wide LD_LIBRARY_PATH entries too.
case "$NPRPC_BUILD_DIR" in
    /*) monitor_library_dir="$NPRPC_BUILD_DIR" ;;
    *) monitor_library_dir="$PWD/$NPRPC_BUILD_DIR" ;;
esac
export LD_LIBRARY_PATH="$monitor_library_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec swift run --package-path nprpc_monitor/app --scratch-path nprpc_monitor/.build NPRPCMonitor "$@"
