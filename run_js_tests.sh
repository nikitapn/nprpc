#!/bin/bash

set -e

./bt.sh nprpc_server_test

cd test/js || exit 1
exec ./run_tests.sh