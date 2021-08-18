@echo off

setlocal
cd /d %~dp0


npidl ^
	--out-ts-dir \\wsl$\Debian\home\png\projects\nprpc\web\src ^
	--out-inc-dir ..\include\nprpc ^
	--out-src-dir ..\nprpcst ^
	nprpc_base.npidl

npidl ^
	--out-ts-dir \\wsl$\Debian\home\png\projects\nprpc\web\src ^
	--out-inc-dir ..\include\nprpc ^
	--out-src-dir ..\include\nprpc ^
	nprpc_nameserver.npidl
