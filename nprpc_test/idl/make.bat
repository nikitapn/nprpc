@echo off

setlocal
cd /d %~dp0


npidl ^
	--gen-ts 0 ^
	--out-inc-dir ..\proxy ^
	--out-src-dir ..\proxy ^
	test.npidl

