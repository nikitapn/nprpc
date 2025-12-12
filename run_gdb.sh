#!/bin/env zsh

gdb -batch -ex "run" -ex "bt f" $1
