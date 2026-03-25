#!/bin/env bash

set -e

source .env

ensure_nprpc_bpf_capabilities() {
	local binary="${1:?binary path is required}"
	local current_caps=""

	command -v getcap >/dev/null 2>&1 || {
		echo "Required command not found: getcap" >&2
		exit 1
	}
	command -v setcap >/dev/null 2>&1 || {
		echo "Required command not found: setcap" >&2
		exit 1
	}

	if [[ ! -x "$binary" ]]; then
		echo "nprpc_server_test not found or not executable: $binary" >&2
		exit 1
	fi

	current_caps="$(getcap "$binary" 2>/dev/null || true)"
	if [[ "$current_caps" == *"cap_net_admin"* && "$current_caps" == *"cap_bpf"* ]]; then
		return 0
	fi

	if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
		setcap cap_net_admin,cap_bpf+ep "$binary"
	else
		# sudo -v || {
		# 	echo "Failed to obtain sudo permissions; cannot grant BPF capabilities to nprpc_server_test" >&2
		# 	exit 1
		# }
		sudo setcap cap_net_admin,cap_bpf+ep "$binary"
	fi
}

cmake --build "$BUILD_DIR" \
	--target nprpc_test nprpc_server_test test_http_utils \
	-j$(nproc)
ensure_nprpc_bpf_capabilities "$BUILD_DIR/test/nprpc_test"
set +e
pkill -9 npnameserver nprpc_server_test 2>/dev/null

timeout 60 ctest --test-dir "$BUILD_DIR/test" --output-on-failure "$@"
# Examples:
#   ./run_cpp_test.sh -R HTTP3Transport
#   ./run_cpp_test.sh -R NprpcTest.TestBasic
echo "Tests exited with code $?"


# Debugging stucked thread during cleanup after test completion
# pkill -9 npnameserver 2>/dev/null; gdb -batch -ex run -ex "thread apply all bt" -ex quit --args "$BUILD_DIR/test/nprpc_test" --gtest_filter="*TestObjectStream*" > /tmp/gdb_obj_stream.log 2>&1; echo "done"
# then killall -11 nprpc_test to trigger the SIGSEGV and get the backtrace in /tmp/gdb_obj_stream.log for Claude to analyze.