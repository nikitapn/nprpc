@echo off

setlocal
cd /d %~dp0


npidl --out-ts-dir \\wsl$\Debian\home\png\projects\web\src ^
	--out-inc-dir ..\include\nprpc ^
	--out-src-dir ..\nprpcst ^
	nprpc_base.npidl

rem npidl --out-ts-dir \\wsl$\Debian\home\png\projects\web\src ^
rem	--out-inc-dir ..\include\nprpc ^
rem	--out-src-dir ..\include\nprpc ^
rem	nprpc_nameserver.npidl
